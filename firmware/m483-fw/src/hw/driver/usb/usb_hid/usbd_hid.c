/*
 * usbd_hid.c
 *
 *  HSUSBD HID 복합장치 클래스 레이어 (QMK 네이티브 3-인터페이스).
 *   IF0 Keyboard : EPA IN 8B  (boot 6KRO, 8K)  + LED SET_REPORT + SET_PROTOCOL(NKRO 판정)
 *   IF1 Raw/VIA  : EPB IN 32B / EPC OUT 32B      (in-place echo)
 *   IF2 Shared   : EPD IN 32B  (NKRO/system/consumer, report-id 멀티플렉스, 8K)
 *
 *  전송은 QMK host_driver_t(port/driver_usb.c)에서만 호출된다.
 */

#include "usbd_hid.h"

#ifdef _USE_HW_USB

#include "cli.h"
#include <string.h>


#define HID_Q_MAX             16      /* 링버퍼 깊이 (2의 거듭제곱) */
#define HID_Q_MASK            (HID_Q_MAX - 1)


/*--------------------------------------------------------------------------*/
/* 키보드(EPA) 리포트 큐 + 레이턴시 계측                                     */
/*--------------------------------------------------------------------------*/
typedef struct
{
  uint8_t  data[HID_KBD_REPORT_SIZE];
  uint16_t length;
  uint32_t press_us;
  uint32_t queue_us;
} kbd_report_t;

static volatile uint8_t  epa_ready = 0;
static kbd_report_t      kbd_q[HID_Q_MAX];
static volatile uint32_t kbd_head = 0;
static volatile uint32_t kbd_tail = 0;

static volatile uint32_t press_us = 0;
static volatile uint32_t inflight_press_us = 0;    /* EPA(6KRO) 경로 */
static volatile uint32_t inflight_queue_us = 0;
static volatile uint32_t inflight_press_us_d = 0;  /* EPD(NKRO) 경로 */
static volatile uint32_t inflight_queue_us_d = 0;

/*--------------------------------------------------------------------------*/
/* Shared(EPD) 리포트 큐 : NKRO / system / consumer                          */
/*--------------------------------------------------------------------------*/
typedef struct
{
  uint8_t  data[SHARED_REPORT_SIZE];
  uint16_t length;
  uint32_t press_us;   /* 접점 시각 (NKRO 눌림 리포트만 != 0) */
  uint32_t queue_us;   /* 큐 적재 시각 */
} shared_report_t;

static volatile uint8_t  epd_ready = 0;
static shared_report_t   shd_q[HID_Q_MAX];
static volatile uint32_t shd_head = 0;
static volatile uint32_t shd_tail = 0;

/*--------------------------------------------------------------------------*/
/* VIA(EPB IN / EPC OUT)                                                     */
/*--------------------------------------------------------------------------*/
static volatile uint8_t        epb_ready = 0;
static usb_hid_via_rx_func_t   via_rx_func = NULL;
static uint8_t                 via_out_buf[VIA_REPORT_SIZE];

/*--------------------------------------------------------------------------*/
/* 측정 / 상태                                                               */
/*--------------------------------------------------------------------------*/
static volatile usb_hid_latency_t   latency;
static volatile usb_hid_rate_info_t rate_info;

static volatile uint32_t sof_count    = 0;
static volatile uint32_t sof_last_us  = 0;
static volatile uint32_t sof_min_us   = 0xFFFFFFFF;
static volatile uint32_t sof_max_us   = 0;
static volatile uint32_t sof_win_ms   = 0;

static volatile uint32_t reset_count   = 0;
static volatile uint32_t suspend_count = 0;

/* USB 링크 헬스 (SOF 순단 감지) */
static volatile uint32_t sof_total       = 0;    /* 모노토닉 SOF 카운터 */
static volatile uint32_t sof_stall_count = 0;
static uint32_t          link_last_sof   = 0;
static uint32_t          link_last_ms    = 0;
static bool              link_sof_seen   = false;

/* 키보드 인터페이스 프로토콜 (NKRO 가능 판정용). 기본 report protocol. */
static volatile uint8_t  kbd_protocol = HID_REPORT_PROTOCOL;

/* 호스트 LED 상태 (SET_REPORT output) */
uint8_t au8LED_Status[8];

#ifdef _USE_HW_CLI
static void cliUsbHid(cli_args_t *args);
#endif


/*--------------------------------------------------------------------------*/
/* Endpoint 구성                                                             */
/*--------------------------------------------------------------------------*/
void usbHidInit(void)
{
  epa_ready = 0;
  epd_ready = 0;
  epb_ready = 0;
  kbd_head = kbd_tail = 0;
  shd_head = shd_tail = 0;

  /* USB/CEP + EPA(kbd IN)/EPB(via IN)/EPC(via OUT)/EPD(shared IN) 글로벌 인터럽트 */
  HSUSBD_ENABLE_USB_INT(HSUSBD_GINTEN_USBIEN_Msk |
                        HSUSBD_GINTEN_CEPIEN_Msk |
                        HSUSBD_GINTEN_EPAIEN_Msk |
                        HSUSBD_GINTEN_EPBIEN_Msk |
                        HSUSBD_GINTEN_EPCIEN_Msk |
                        HSUSBD_GINTEN_EPDIEN_Msk);

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

  /* EPA : keyboard interrupt IN */
  HSUSBD_SetEpBufAddr(EPA, EPA_BUF_BASE, EPA_BUF_LEN);
  HSUSBD_SET_MAX_PAYLOAD(EPA, EPA_MAX_PKT_SIZE);
  HSUSBD_ConfigEp(EPA, INT_IN_EP_NUM_KB, HSUSBD_EP_CFG_TYPE_INT, HSUSBD_EP_CFG_DIR_IN);

  /* EPB : VIA interrupt IN */
  HSUSBD_SetEpBufAddr(EPB, EPB_BUF_BASE, EPB_BUF_LEN);
  HSUSBD_SET_MAX_PAYLOAD(EPB, EPB_MAX_PKT_SIZE);
  HSUSBD_ConfigEp(EPB, INT_IN_EP_NUM_VIA, HSUSBD_EP_CFG_TYPE_INT, HSUSBD_EP_CFG_DIR_IN);

  /* EPC : VIA interrupt OUT */
  HSUSBD_SetEpBufAddr(EPC, EPC_BUF_BASE, EPC_BUF_LEN);
  HSUSBD_SET_MAX_PAYLOAD(EPC, EPC_MAX_PKT_SIZE);
  HSUSBD_ConfigEp(EPC, OUT_EP_NUM_VIA, HSUSBD_EP_CFG_TYPE_INT, HSUSBD_EP_CFG_DIR_OUT);
  HSUSBD_ENABLE_EP_INT(EPC, HSUSBD_EPINTEN_RXPKIEN_Msk);

  /* EPD : shared interrupt IN (NKRO/system/consumer) */
  HSUSBD_SetEpBufAddr(EPD, EPD_BUF_BASE, EPD_BUF_LEN);
  HSUSBD_SET_MAX_PAYLOAD(EPD, EPD_MAX_PKT_SIZE);
  HSUSBD_ConfigEp(EPD, INT_IN_EP_NUM_SHARED, HSUSBD_EP_CFG_TYPE_INT, HSUSBD_EP_CFG_DIR_IN);

  epa_ready = 1;
  epb_ready = 1;
  epd_ready = 1;
}

/*--------------------------------------------------------------------------*/
/* HID class request                                                        */
/*--------------------------------------------------------------------------*/
void HID_ClassRequest(void)
{
  static uint8_t u8Idle = 0;

  if (gUsbCmd.bmRequestType & 0x80)   /* Device to host */
  {
    switch (gUsbCmd.bRequest)
    {
      case HID_GET_REPORT:
      {
        static uint8_t u8Report = 0;
        HSUSBD_PrepareCtrlIn(&u8Report, 1ul);
        HSUSBD_CLR_CEP_INT_FLAG(HSUSBD_CEPINTSTS_INTKIF_Msk);
        HSUSBD_ENABLE_CEP_INT(HSUSBD_CEPINTEN_INTKIEN_Msk);
        break;
      }
      case HID_GET_IDLE:
        HSUSBD_PrepareCtrlIn(&u8Idle, 1ul);
        HSUSBD_CLR_CEP_INT_FLAG(HSUSBD_CEPINTSTS_INTKIF_Msk);
        HSUSBD_ENABLE_CEP_INT(HSUSBD_CEPINTEN_INTKIEN_Msk);
        break;

      case HID_GET_PROTOCOL:
      {
        static uint8_t u8Proto;
        u8Proto = kbd_protocol;
        HSUSBD_PrepareCtrlIn(&u8Proto, 1ul);
        HSUSBD_CLR_CEP_INT_FLAG(HSUSBD_CEPINTSTS_INTKIF_Msk);
        HSUSBD_ENABLE_CEP_INT(HSUSBD_CEPINTEN_INTKIEN_Msk);
        break;
      }
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
        /* 키보드 인터페이스(IF0)의 boot/report 프로토콜을 추적 -> NKRO 가능 판정 */
        if (gUsbCmd.wIndex == IF_NUM_KBD)
        {
          kbd_protocol = (gUsbCmd.wValue == HID_BOOT_PROTOCOL) ?
                         HID_BOOT_PROTOCOL : HID_REPORT_PROTOCOL;
        }
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
  HSUSBD_SET_CEP_STATE(HSUSBD_CEPCTL_STALLEN_Msk);
}


/*--------------------------------------------------------------------------*/
/* 키보드(EPA) 전송 : 큐 + flush + 레이턴시                                   */
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

  next = (kbd_head + 1) & HID_Q_MASK;
  if (next == kbd_tail)
    kbd_tail = (kbd_tail + 1) & HID_Q_MASK;   /* full -> drop oldest */

  memset(kbd_q[kbd_head].data, 0, HID_KBD_REPORT_SIZE);
  memcpy(kbd_q[kbd_head].data, p_data, length);
  kbd_q[kbd_head].length   = length;
  kbd_q[kbd_head].press_us = press_us;
  kbd_q[kbd_head].queue_us = micros();
  /* press_us 소비 : 새 눌림에만 matrix 가 다시 세팅한다. 릴리즈/후속 리포트는
   * press_us=0 -> EP 핸들러가 raw/pre 를 건너뛰어, log 가 항상 '눌림 지연'만 보이게 함. */
  press_us = 0;
  kbd_head = next;

  if (!pri_mask)
    __enable_irq();

  usbHidFlush();
  return true;
}

/* shared(EPD) 큐 적재 헬퍼 (NKRO/system/consumer 공용) */
static bool shared_enqueue(uint8_t *p_data, uint16_t length)
{
  uint32_t next;
  uint32_t pri_mask;

  if (p_data == NULL)
    return false;
  if (length > SHARED_REPORT_SIZE)
    length = SHARED_REPORT_SIZE;

  pri_mask = __get_PRIMASK();
  __disable_irq();

  next = (shd_head + 1) & HID_Q_MASK;
  if (next == shd_tail)
    shd_tail = (shd_tail + 1) & HID_Q_MASK;

  memcpy(shd_q[shd_head].data, p_data, length);
  shd_q[shd_head].length   = length;
  shd_q[shd_head].press_us = press_us;
  shd_q[shd_head].queue_us = micros();
  /* press_us 소비 : NKRO 모드에선 키 눌림이 EPA 대신 이 EPD 경로로 나가므로
   * 여기서 접점시각을 소비해야 레이턴시가 측정된다. 릴리즈/system/consumer/mouse 는
   * press_us=0 -> EP 핸들러가 측정을 건너뛴다. */
  press_us = 0;
  shd_head = next;

  if (!pri_mask)
    __enable_irq();

  usbHidFlush();
  return true;
}

bool usbHidSendReportNkro(uint8_t *p_data, uint16_t length)
{
  return shared_enqueue(p_data, length);
}

bool usbHidSendReportEXK(uint8_t *p_data, uint16_t length)
{
  return shared_enqueue(p_data, length);
}

bool usbHidSendReportMouse(uint8_t *p_data, uint16_t length)
{
  return shared_enqueue(p_data, length);   /* report id 2, shared EP(EPD) */
}

void usbHidFlush(void)
{
  uint32_t pri_mask;
  uint16_t i;

  pri_mask = __get_PRIMASK();
  __disable_irq();

  /* 키보드(EPA) */
  if (epa_ready && (kbd_tail != kbd_head))
  {
    kbd_report_t *r = &kbd_q[kbd_tail];

    inflight_press_us = r->press_us;
    inflight_queue_us = r->queue_us;
    epa_ready = 0;

    /* FIFO 워드(4B) write : data[] 는 4B 정렬. 8B -> 2회 (바이트 8회 대비 단축) */
    {
      const uint32_t *w = (const uint32_t *)(const void *)r->data;
      for (i = 0; i < HID_KBD_REPORT_SIZE / 4; i++)
        HSUSBD->EP[EPA].EPDAT = w[i];
    }
    HSUSBD->EP[EPA].EPRSPCTL = HSUSBD_EP_RSPCTL_SHORTTXEN;
    HSUSBD_ENABLE_EP_INT(EPA, HSUSBD_EPINTEN_TXPKIEN_Msk);

    kbd_tail = (kbd_tail + 1) & HID_Q_MASK;
  }

  /* Shared(EPD) : NKRO/system/consumer */
  if (epd_ready && (shd_tail != shd_head))
  {
    shared_report_t *r = &shd_q[shd_tail];

    inflight_press_us_d = r->press_us;
    inflight_queue_us_d = r->queue_us;
    epd_ready = 0;

    /* FIFO 워드(4B) write + 나머지 바이트. NKRO 32B -> 8회 (바이트 32회 대비 단축) */
    {
      const uint32_t *w  = (const uint32_t *)(const void *)r->data;
      uint16_t        nw = r->length >> 2;
      for (i = 0; i < nw; i++)
        HSUSBD->EP[EPD].EPDAT = w[i];
      for (i = nw << 2; i < r->length; i++)
        HSUSBD->EP[EPD].EPDAT_BYTE = r->data[i];
    }
    HSUSBD->EP[EPD].EPRSPCTL = HSUSBD_EP_RSPCTL_SHORTTXEN;
    HSUSBD_ENABLE_EP_INT(EPD, HSUSBD_EPINTEN_TXPKIEN_Msk);

    shd_tail = (shd_tail + 1) & HID_Q_MASK;
  }

  if (!pri_mask)
    __enable_irq();
}

/* 키보드(EPA) 전송 완료 (IRQ, TXPKIF) */
void usbHidEpAHandler(void)
{
  uint32_t tx_done_us = micros();

  /* 눌림 리포트(press_us!=0)에만 3값을 함께 갱신 -> 항상 raw = pre + usb, 실제 눌림 지연만.
   * 릴리즈/후속 리포트(press_us=0)는 측정에서 제외한다. */
  if (inflight_press_us != 0)
  {
    latency.usb_us = (uint16_t)(tx_done_us - inflight_queue_us);
    latency.raw_us = (uint16_t)(tx_done_us - inflight_press_us);
    latency.pre_us = (uint16_t)(inflight_queue_us - inflight_press_us);
    latency.seq++;
    inflight_press_us = 0;
  }
  inflight_queue_us = 0;

  epa_ready = 1;
  usbHidFlush();
}

/* Shared(EPD) 전송 완료 (IRQ, TXPKIF) */
void usbHidEpDHandler(void)
{
  uint32_t tx_done_us = micros();

  /* NKRO 눌림 리포트(press_us!=0)에만 레이턴시 갱신 -> raw = pre + usb.
   * 릴리즈/system/consumer/mouse(press_us=0) 는 측정 제외. (EPA 핸들러와 동일) */
  if (inflight_press_us_d != 0)
  {
    latency.usb_us = (uint16_t)(tx_done_us - inflight_queue_us_d);
    latency.raw_us = (uint16_t)(tx_done_us - inflight_press_us_d);
    latency.pre_us = (uint16_t)(inflight_queue_us_d - inflight_press_us_d);
    latency.seq++;
    inflight_press_us_d = 0;
  }
  inflight_queue_us_d = 0;

  epd_ready = 1;
  usbHidFlush();
}


/*--------------------------------------------------------------------------*/
/* VIA (EPB IN 응답 / EPC OUT 수신)                                          */
/*--------------------------------------------------------------------------*/
bool usbHidSendReportVia(uint8_t *p_data, uint16_t length)
{
  uint16_t i;
  uint32_t pri_mask;

  if (p_data == NULL)
    return false;
  if (length > VIA_REPORT_SIZE)
    length = VIA_REPORT_SIZE;

  pri_mask = __get_PRIMASK();
  __disable_irq();

  if (epb_ready)
  {
    epb_ready = 0;
    for (i = 0; i < length; i++)
      HSUSBD->EP[EPB].EPDAT_BYTE = p_data[i];
    HSUSBD->EP[EPB].EPRSPCTL = HSUSBD_EP_RSPCTL_SHORTTXEN;
    HSUSBD_ENABLE_EP_INT(EPB, HSUSBD_EPINTEN_TXPKIEN_Msk);
  }

  if (!pri_mask)
    __enable_irq();
  return true;
}

void usbHidSetViaReceiveFunc(usb_hid_via_rx_func_t fn)
{
  via_rx_func = fn;
}

/* EPB VIA IN 전송 완료 */
void usbHidEpBHandler(void)
{
  epb_ready = 1;
}

/* EPC VIA OUT 수신 : 버퍼 읽기 -> 콜백(in-place 처리) -> EPB 로 응답 echo */
void usbHidEpCHandler(void)
{
  uint32_t len;
  uint32_t i;

  len = HSUSBD->EP[EPC].EPDATCNT & 0xFFFF;
  if (len > VIA_REPORT_SIZE)
    len = VIA_REPORT_SIZE;

  for (i = 0; i < len; i++)
    via_out_buf[i] = HSUSBD->EP[EPC].EPDAT_BYTE;

  if (via_rx_func != NULL)
  {
    via_rx_func(via_out_buf, (uint8_t)len);      /* raw_hid_receive : in-place 응답 작성 */
    usbHidSendReportVia(via_out_buf, VIA_REPORT_SIZE);
  }
}


/*--------------------------------------------------------------------------*/
/* SOF 폴링레이트 측정                                                       */
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
  sof_total++;

  if (sof_win_ms == 0)
    sof_win_ms = now_ms;

  if ((now_ms - sof_win_ms) >= 1000)
  {
    rate_info.freq_hz  = sof_count;
    rate_info.time_min = (sof_min_us == 0xFFFFFFFF) ? 0 : sof_min_us;
    rate_info.time_max = sof_max_us;

    sof_count  = 0;
    sof_min_us = 0xFFFFFFFF;
    sof_max_us = 0;
    sof_win_ms = now_ms;
  }
}

void usbHidOnBusReset(void)
{
  reset_count++;
  sof_last_us = 0;
  kbd_protocol = HID_REPORT_PROTOCOL;   /* 리셋 시 기본 report 로 복귀 */
}

void usbHidOnSuspend(void)
{
  suspend_count++;
}


/*--------------------------------------------------------------------------*/
/* getter / setter                                                          */
/*--------------------------------------------------------------------------*/
uint8_t usbHidGetKbdLeds(void)
{
  return au8LED_Status[0];
}

bool usbHidKbdIsReportProtocol(void)
{
  return (kbd_protocol == HID_REPORT_PROTOCOL);
}

bool usbHidIsReady(void)
{
  return (g_hsusbd_Configured != 0);
}

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

/*--------------------------------------------------------------------------*/
/* USB 링크 헬스 (웹 USB 점검)                                              */
/*--------------------------------------------------------------------------*/
void usbHidGetLinkHealth(usb_link_health_t *p_info)
{
  if (p_info == NULL)
    return;
  p_info->reset_count     = reset_count;
  p_info->suspend_count   = suspend_count;
  p_info->sof_stall_count = sof_stall_count;
  p_info->sof_rate        = rate_info.freq_hz;
  p_info->uptime_s        = millis() / 1000;
}

void usbHidResetLinkHealth(void)
{
  reset_count     = 0;
  suspend_count   = 0;
  sof_stall_count = 0;
}

/* SOF 순단 감지 : 5ms 마다 SOF 카운터가 안 늘면(=버스 프레임 정지) stall 카운트. */
void usbLinkFramePoll(void)
{
  uint32_t now;

  if (g_hsusbd_Configured == 0)
  {
    link_sof_seen = false;
    return;
  }

  now = millis();
  if ((now - link_last_ms) < 5)
    return;
  link_last_ms = now;

  if (link_sof_seen && (sof_total == link_last_sof))
    sof_stall_count++;          /* 열거된 상태인데 5ms 동안 SOF 0개 -> 순단 */

  link_last_sof = sof_total;
  link_sof_seen = true;
}

__attribute__((weak)) void usbHidSetStatusLed(uint8_t led_bits)
{
  (void)led_bits;
}

/* 현재 NKRO 활성 여부 : keymap_config.nkro 는 QMK 레이어에만 있으므로 weak 훅.
 * driver_usb.c 가 (report protocol && keymap_config.nkro) 로 재정의한다. */
__attribute__((weak)) bool usbHidNkroActive(void)
{
  return false;
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
    cliPrintf("protocol     : %s\n", usbHidKbdIsReportProtocol() ? "report(NKRO ok)" : "boot");
    cliPrintf("kbd mode     : %s\n", usbHidNkroActive() ? "NKRO (EPD/shared)" : "6KRO (EPA)");
    cliPrintf("sof rate     : %d Hz\n", (int)info.freq_hz);
    cliPrintf("reset count  : %d\n", (int)reset_count);
    cliPrintf("suspend count: %d\n", (int)suspend_count);
    cliPrintf("host leds    : 0x%02X\n", au8LED_Status[0]);
    ret = true;
  }

  if (ret == false)
  {
    cliPrintf("usbhid rate\n");
    cliPrintf("usbhid log\n");
    cliPrintf("usbhid info\n");
  }
}
#endif

#endif /* _USE_HW_USB */
