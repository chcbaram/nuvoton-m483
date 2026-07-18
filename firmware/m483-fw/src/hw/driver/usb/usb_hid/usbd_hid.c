/*
 * usbd_hid.c
 *
 *  HSUSBD HID keyboard class layer.
 *  - endpoint 구성, HID class 요청 처리 (Nuvoton BSP HSUSBD_HID_MouseKeyboard 이식)
 *  - 리포트 큐 + usbHidSendReport()/usbHidFlush() (baram-qmk-8k 패턴 미러링)
 *  - 폴링레이트(SOF) / 키스트로크 레이턴시 측정
 */

#include "usbd_hid.h"

#ifdef _USE_HW_USB

#include "cli.h"


#define HID_Q_MAX             16      /* 리포트 링버퍼 깊이 (2의 거듭제곱) */
#define HID_Q_MASK            (HID_Q_MAX - 1)


typedef struct
{
  uint8_t  data[HID_KBD_REPORT_SIZE];
  uint16_t length;
  uint32_t press_us;     /* 접점(버튼) 발생 시각 */
  uint32_t queue_us;     /* 큐 적재 시각 */
} hid_report_t;


static volatile uint8_t  is_ep_ready = 0;

static hid_report_t      report_q[HID_Q_MAX];
static volatile uint32_t q_head = 0;
static volatile uint32_t q_tail = 0;

/* 현재 접점 시각 (버튼 눌림/뗌 에지에서 갱신) */
static volatile uint32_t press_us = 0;

/* 전송 중(in-flight) 리포트의 타임스탬프 */
static volatile uint32_t inflight_press_us = 0;
static volatile uint32_t inflight_queue_us = 0;

/* 측정 결과 */
static volatile usb_hid_latency_t  latency;
static volatile usb_hid_rate_info_t rate_info;

/* SOF 기반 폴링레이트 측정 */
static volatile uint32_t sof_count    = 0;
static volatile uint32_t sof_last_us  = 0;
static volatile uint32_t sof_min_us   = 0xFFFFFFFF;
static volatile uint32_t sof_max_us   = 0;
static volatile uint32_t sof_win_ms   = 0;

/* 링크 상태 */
static volatile uint32_t reset_count   = 0;
static volatile uint32_t suspend_count = 0;

/* 호스트 LED 상태 */
uint8_t au8LED_Status[8];

#ifdef _USE_HW_CLI
static void cliUsbHid(cli_args_t *args);
#endif


void usbHidInit(void)
{
  is_ep_ready = 0;
  q_head = 0;
  q_tail = 0;

  /* Enable USB BUS, CEP and EPA global interrupt */
  HSUSBD_ENABLE_USB_INT(HSUSBD_GINTEN_USBIEN_Msk |
                        HSUSBD_GINTEN_CEPIEN_Msk |
                        HSUSBD_GINTEN_EPAIEN_Msk);

  /* Enable BUS interrupt (+ SOF for polling-rate measurement) */
  HSUSBD_ENABLE_BUS_INT(HSUSBD_BUSINTEN_DMADONEIEN_Msk |
                        HSUSBD_BUSINTEN_RESUMEIEN_Msk |
                        HSUSBD_BUSINTEN_RSTIEN_Msk |
                        HSUSBD_BUSINTEN_VBUSDETIEN_Msk |
                        HSUSBD_BUSINTEN_SUSPENDIEN_Msk |
                        HSUSBD_BUSINTEN_SOFIEN_Msk);

  HSUSBD_SET_ADDR(0);

  /* Control endpoint */
  HSUSBD_SetEpBufAddr(CEP, CEP_BUF_BASE, CEP_BUF_LEN);
  HSUSBD_ENABLE_CEP_INT(HSUSBD_CEPINTEN_SETUPPKIEN_Msk | HSUSBD_CEPINTEN_STSDONEIEN_Msk);

  /* EPA ==> Interrupt IN endpoint, keyboard */
  HSUSBD_SetEpBufAddr(EPA, EPA_BUF_BASE, EPA_BUF_LEN);
  HSUSBD_SET_MAX_PAYLOAD(EPA, EPA_MAX_PKT_SIZE);
  HSUSBD_ConfigEp(EPA, INT_IN_EP_NUM_KB, HSUSBD_EP_CFG_TYPE_INT, HSUSBD_EP_CFG_DIR_IN);

  is_ep_ready = 1;
}

/*--------------------------------------------------------------------------*/
/* HID class request (keyboard interface 0)                                 */
/*--------------------------------------------------------------------------*/
void HID_ClassRequest(void)
{
  static uint8_t u8Report = 0;
  static uint8_t u8Idle   = 0;

  if (gUsbCmd.bmRequestType & 0x80)   /* Device to host */
  {
    switch (gUsbCmd.bRequest)
    {
      case HID_GET_REPORT:
        HSUSBD_PrepareCtrlIn(&u8Report, 1ul);
        HSUSBD_CLR_CEP_INT_FLAG(HSUSBD_CEPINTSTS_INTKIF_Msk);
        HSUSBD_ENABLE_CEP_INT(HSUSBD_CEPINTEN_INTKIEN_Msk);
        break;

      case HID_GET_IDLE:
        HSUSBD_PrepareCtrlIn(&u8Idle, 1ul);
        HSUSBD_CLR_CEP_INT_FLAG(HSUSBD_CEPINTSTS_INTKIF_Msk);
        HSUSBD_ENABLE_CEP_INT(HSUSBD_CEPINTEN_INTKIEN_Msk);
        break;

      default:
        HSUSBD_SET_CEP_STATE(HSUSBD_CEPCTL_STALLEN_Msk);
        break;
    }
  }
  else                                /* Host to device */
  {
    switch (gUsbCmd.bRequest)
    {
      case HID_SET_REPORT:
        if (((gUsbCmd.wValue >> 8) & 0xff) == 2)  /* Output report (LED) */
        {
          if (HSUSBD_CtrlOut(au8LED_Status, (gUsbCmd.wLength & 0xff)) == HSUSBD_ERR_TIMEOUT)
            HSUSBD_SET_CEP_STATE(HSUSBD_CEPCTL_FLUSH);

          HSUSBD_CLR_CEP_INT_FLAG(HSUSBD_CEPINTSTS_RXPKIF_Msk);
          HSUSBD_ENABLE_CEP_INT(HSUSBD_CEPINTEN_RXPKIEN_Msk);

          HSUSBD_CLR_CEP_INT_FLAG(HSUSBD_CEPINTSTS_STSDONEIF_Msk);
          HSUSBD_SET_CEP_STATE(HSUSBD_CEPCTL_NAKCLR);
          HSUSBD_ENABLE_CEP_INT(HSUSBD_CEPINTEN_STSDONEIEN_Msk);

          usbHidSetStatusLed(au8LED_Status[0]);
        }
        else
        {
          /* Feature/other: just ack status stage */
          HSUSBD_CLR_CEP_INT_FLAG(HSUSBD_CEPINTSTS_STSDONEIF_Msk);
          HSUSBD_SET_CEP_STATE(HSUSBD_CEPCTL_NAKCLR);
          HSUSBD_ENABLE_CEP_INT(HSUSBD_CEPINTEN_STSDONEIEN_Msk);
        }
        break;

      case HID_SET_IDLE:
        u8Idle = (gUsbCmd.wValue >> 8) & 0xff;
        HSUSBD_CLR_CEP_INT_FLAG(HSUSBD_CEPINTSTS_STSDONEIF_Msk);
        HSUSBD_SET_CEP_STATE(HSUSBD_CEPCTL_NAKCLR);
        HSUSBD_ENABLE_CEP_INT(HSUSBD_CEPINTEN_STSDONEIEN_Msk);
        break;

      case HID_SET_PROTOCOL:
        HSUSBD_CLR_CEP_INT_FLAG(HSUSBD_CEPINTSTS_STSDONEIF_Msk);
        HSUSBD_SET_CEP_STATE(HSUSBD_CEPCTL_NAKCLR);
        HSUSBD_ENABLE_CEP_INT(HSUSBD_CEPINTEN_STSDONEIEN_Msk);
        break;

      default:
        HSUSBD_SET_CEP_STATE(HSUSBD_CEPCTL_STALLEN_Msk);
        break;
    }
  }
}

void HID_VendorRequest(void)
{
  /* No vendor request supported: stall */
  HSUSBD_SET_CEP_STATE(HSUSBD_CEPCTL_STALLEN_Msk);
}

/*--------------------------------------------------------------------------*/
/* Report queue + flush                                                     */
/*--------------------------------------------------------------------------*/
bool usbHidSendReport(uint8_t *p_data, uint16_t length)
{
  uint32_t next;
  uint32_t pri_mask;

  if (p_data == NULL)
    return false;

  if (length > HID_KBD_REPORT_SIZE)
    length = HID_KBD_REPORT_SIZE;

  pri_mask = __get_PRIMASK();
  __disable_irq();

  next = (q_head + 1) & HID_Q_MASK;
  if (next == q_tail)
  {
    /* 큐가 가득 참 -> 가장 오래된 리포트 폐기 (호스트가 stale 상태를 붙잡지 않도록) */
    q_tail = (q_tail + 1) & HID_Q_MASK;
  }

  memset(report_q[q_head].data, 0, HID_KBD_REPORT_SIZE);
  memcpy(report_q[q_head].data, p_data, length);
  report_q[q_head].length   = length;
  report_q[q_head].press_us = press_us;
  report_q[q_head].queue_us = micros();
  q_head = next;

  if (!pri_mask)
    __enable_irq();

  usbHidFlush();
  return true;
}

void usbHidFlush(void)
{
  uint32_t pri_mask;
  hid_report_t *p_report;
  uint16_t i;

  pri_mask = __get_PRIMASK();
  __disable_irq();

  if (is_ep_ready && (q_tail != q_head))
  {
    p_report = &report_q[q_tail];

    inflight_press_us = p_report->press_us;
    inflight_queue_us = p_report->queue_us;

    is_ep_ready = 0;

    for (i = 0; i < HID_KBD_REPORT_SIZE; i++)
      HSUSBD->EP[EPA].EPDAT_BYTE = p_report->data[i];

    HSUSBD->EP[EPA].EPRSPCTL = HSUSBD_EP_RSPCTL_SHORTTXEN;
    /* TXPKIF: 데이터 패킷이 실제로 호스트에 전송 완료된 시점에 인터럽트 */
    HSUSBD_ENABLE_EP_INT(EPA, HSUSBD_EPINTEN_TXPKIEN_Msk);

    q_tail = (q_tail + 1) & HID_Q_MASK;
  }

  if (!pri_mask)
    __enable_irq();
}

/* 키보드 EP(EPA) 패킷 전송 완료 (IRQ, TXPKIF) */
void usbHidEpHandler(void)
{
  uint32_t tx_done_us = micros();   /* 패킷이 호스트로 전송 완료된 시점 */

  /* 레이턴시 계산 (in-flight 리포트 기준) */
  if (inflight_queue_us != 0)
  {
    latency.usb_us = (uint16_t)(tx_done_us - inflight_queue_us);
    if (inflight_press_us != 0)
    {
      latency.raw_us = (uint16_t)(tx_done_us - inflight_press_us);
      latency.pre_us = (uint16_t)(inflight_queue_us - inflight_press_us);
    }
    latency.seq++;
    inflight_queue_us = 0;
  }

  is_ep_ready = 1;
  usbHidFlush();
}

/*--------------------------------------------------------------------------*/
/* SOF 기반 폴링레이트 (버스 마이크로프레임 심박) 측정                       */
/*--------------------------------------------------------------------------*/
void usbHidOnSof(void)
{
  uint32_t now_us = micros();
  uint32_t now_ms = millis();

  if (sof_last_us != 0)
  {
    uint32_t dt = now_us - sof_last_us;
    if (dt < sof_min_us) sof_min_us = dt;
    if (dt > sof_max_us) sof_max_us = dt;
  }
  sof_last_us = now_us;
  sof_count++;

  if (sof_win_ms == 0)
    sof_win_ms = now_ms;

  if ((now_ms - sof_win_ms) >= 1000)
  {
    rate_info.freq_hz  = sof_count;
    rate_info.time_min = (sof_min_us == 0xFFFFFFFF) ? 0 : sof_min_us;
    rate_info.time_max = sof_max_us;

    sof_count   = 0;
    sof_min_us  = 0xFFFFFFFF;
    sof_max_us  = 0;
    sof_win_ms  = now_ms;
  }
}

void usbHidOnBusReset(void)
{
  reset_count++;
  sof_last_us = 0;
}

void usbHidOnSuspend(void)
{
  suspend_count++;
}

/*--------------------------------------------------------------------------*/
/* 측정 결과 getter / setter                                                */
/*--------------------------------------------------------------------------*/
bool usbHidGetRateInfo(usb_hid_rate_info_t *p_info)
{
  if (p_info == NULL)
    return false;

  p_info->freq_hz  = rate_info.freq_hz;
  p_info->time_max = rate_info.time_max;
  p_info->time_min = rate_info.time_min;
  return true;
}

bool usbHidGetLatency(uint16_t *raw_us, uint16_t *pre_us, uint16_t *usb_us, uint32_t *seq)
{
  if (raw_us) *raw_us = latency.raw_us;
  if (pre_us) *pre_us = latency.pre_us;
  if (usb_us) *usb_us = latency.usb_us;
  if (seq)    *seq    = latency.seq;
  return true;
}

bool usbHidSetPressTime(uint32_t time_us)
{
  press_us = time_us;
  return true;
}

__attribute__((weak)) void usbHidSetStatusLed(uint8_t led_bits)
{
  (void)led_bits;
}


/*--------------------------------------------------------------------------*/
/* CLI                                                                      */
/*--------------------------------------------------------------------------*/
void usbHidCliInit(void)
{
#ifdef _USE_HW_CLI
  cliAdd("usbhid", cliUsbHid);
#endif
}

#ifdef _USE_HW_CLI
static void cliUsbHid(cli_args_t *args)
{
  bool ret = false;

  if (args->argc == 1 && args->isStr(0, "rate"))
  {
    usb_hid_rate_info_t info;

    cliPrintf("SOF(bus microframe) rate 측정 (Ctrl-C 종료)\n");
    while (cliKeepLoop())
    {
      usbHidGetRateInfo(&info);
      cliPrintf("rate %d Hz, min %d us, max %d us\r",
                (int)info.freq_hz, (int)info.time_min, (int)info.time_max);
      delay(200);
    }
    cliPrintf("\n");
    ret = true;
  }

  if (args->argc == 1 && args->isStr(0, "log"))
  {
    uint16_t raw, pre, usb;
    uint32_t seq;

    cliPrintf("keystroke latency (Ctrl-C 종료)\n");
    while (cliKeepLoop())
    {
      usbHidGetLatency(&raw, &pre, &usb, &seq);
      cliPrintf("seq %d : raw %d us, pre %d us, usb %d us\r",
                (int)seq, (int)raw, (int)pre, (int)usb);
      delay(200);
    }
    cliPrintf("\n");
    ret = true;
  }

  if (args->argc == 1 && args->isStr(0, "info"))
  {
    usb_hid_rate_info_t info;
    usbHidGetRateInfo(&info);
    cliPrintf("configured   : %s\n", g_hsusbd_Configured ? "yes" : "no");
    cliPrintf("sof rate     : %d Hz\n", (int)info.freq_hz);
    cliPrintf("reset count  : %d\n", (int)reset_count);
    cliPrintf("suspend count: %d\n", (int)suspend_count);
    ret = true;
  }

  if (args->argc == 2 && args->isStr(0, "send"))
  {
    uint8_t  key = (uint8_t)args->getData(1);
    uint8_t  report[HID_KBD_REPORT_SIZE] = {0, };

    report[2] = key;
    usbHidSendReport(report, HID_KBD_REPORT_SIZE);
    delay(20);
    memset(report, 0, HID_KBD_REPORT_SIZE);
    usbHidSendReport(report, HID_KBD_REPORT_SIZE);
    cliPrintf("send keycode 0x%02X\n", key);
    ret = true;
  }

  if (ret == false)
  {
    cliPrintf("usbhid rate\n");
    cliPrintf("usbhid log\n");
    cliPrintf("usbhid info\n");
    cliPrintf("usbhid send [keycode]\n");
  }
}
#endif

#endif /* _USE_HW_USB */
