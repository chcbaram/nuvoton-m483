/*
 * ws2812.c
 *
 *  WS2812(NeoPixel) 드라이버 - EPWM0 + PDMA 듀티 스트리밍 방식.
 *
 *  - 회로도: MCU NEOPIXEL_3V3(PA.5) -> TXB0101 레벨시프터(3V3->5V) ->
 *    SK6812MINI-E 45개 직렬 체인(K1..K45, GRB).
 *  - 방식: EPWM0 를 800kHz(1.25us) 로 돌려 WS2812 파형을 생성하고, 비트 하나당
 *    EPWM 한 주기의 듀티(CMPDAT)를 PDMA 로 갱신한다. 갱신 트리거는 **TMR3(정확히
 *    800kHz)** 가 매 주기 1회 PDMA 를 트리거한다. (EPWM 어큐뮬레이터는 IFACNT=1 에서도
 *    2주기마다 트리거되어 비트가 2배로 늘어나는 문제가 있어 사용하지 않는다.)
 *    프레임 전체가 PDMA 한 번으로 전송되고 완료는 PDMA transfer-done 플래그를
 *    폴링한다(인터럽트 0회, CPU 논블로킹).
 *  - baram-qmk-8k 의 ws2812.c(TIM PWM + DMA)와 동일 개념을 Nuvoton EPWM+TMR 로 이식.
 */

#include "ws2812.h"


#ifdef _USE_HW_WS2812
#include "cli.h"


/*----------------------------------------------------------------------------*/
/* 보드 배선 설정                                                             */
/* NEOPIXEL_3V3 핀 = PA.5 (EPWM0_CH0). 회로도 확인 완료.                      */
/* PA.4(EPWM0_CH1) 로 바꾸려면 WS2812_ON_PA4 를 1 로 변경.                     */
/*----------------------------------------------------------------------------*/
#define WS2812_ON_PA4           0

#if WS2812_ON_PA4
  #define WS2812_EPWM_CH        1
#else
  #define WS2812_EPWM_CH        0
#endif

#define WS2812_TIMER            TIMER3        /* 듀티 갱신 트리거용 타이머 */
#define WS2812_PDMA_REQ         PDMA_TMR3
#define WS2812_PDMA_CH          0             /* WS2812 전용 PDMA 채널 */

/*
 * 타이밍 : 실장 LED 는 SK6812MINI-E (WS2812 호환, GRB).
 * bit 주기 1.25us(800kHz). baram-qmk 와 동일하게 T0H=0.35us, T1H=0.70us 사용
 * (SK6812/WS2812 양쪽 허용범위).
 */
#define WS2812_FREQ_HZ          800000
#define WS2812_T0H_NS           350
#define WS2812_T1H_NS           700
#define WS2812_BIT_NS           1250

/*
 * reset(low) 프리앰블. 데이터 앞/뒤로 라인을 충분히 low 로 유지해 스트링이
 * 픽셀 경계를 잡고(latch) 리셋되도록 한다. 이게 없으면 색이 뒤틀린다.
 * 1.25us * 90 = 112.5us (SK6812 요구 >80us 충족). baram 의 BIT_ZERO 프리앰블과 동일 개념.
 */
#define WS2812_RESET_LEAD       90
#define WS2812_RESET_TAIL       90

#define WS2812_BITS_PER_LED     24
#define WS2812_BUF_LEN          (WS2812_RESET_LEAD + WS2812_MAX_CH * WS2812_BITS_PER_LED + WS2812_RESET_TAIL)


static bool     is_init = false;
static bool     is_busy = false;

static uint16_t cmr_buf[WS2812_BUF_LEN];   /* 비트별 EPWM CMPDAT(듀티) 값 */
static uint16_t cmr_zero = 0;
static uint16_t cmr_one  = 0;

#if CLI_USE(HW_WS2812)
static void cliWs2812(cli_args_t *args);
#endif


bool ws2812Init(void)
{
  uint32_t cnr;

  memset(cmr_buf, 0, sizeof(cmr_buf));

  /* ---- clock ---- */
  SYS_UnlockReg();
  CLK_EnableModuleClock(EPWM0_MODULE);
  CLK_SetModuleClock(EPWM0_MODULE, CLK_CLKSEL2_EPWM0SEL_PCLK0, (uint32_t)NULL);
  CLK_EnableModuleClock(TMR3_MODULE);
  CLK_SetModuleClock(TMR3_MODULE, CLK_CLKSEL1_TMR3SEL_PCLK1, (uint32_t)NULL);
  CLK_EnableModuleClock(PDMA_MODULE);

  /* ---- pin MFP : NEOPIXEL_3V3 = EPWM0 output ---- */
#if WS2812_ON_PA4
  SYS->GPA_MFPL = (SYS->GPA_MFPL & ~SYS_GPA_MFPL_PA4MFP_Msk) | SYS_GPA_MFPL_PA4MFP_EPWM0_CH1;
#else
  SYS->GPA_MFPL = (SYS->GPA_MFPL & ~SYS_GPA_MFPL_PA5MFP_Msk) | SYS_GPA_MFPL_PA5MFP_EPWM0_CH0;
#endif
  SYS_LockReg();

  /* ---- EPWM 800kHz 설정 ---- */
  EPWM_ConfigOutputChannel(EPWM0, WS2812_EPWM_CH, WS2812_FREQ_HZ, 30);
  EPWM_EnableOutput(EPWM0, (1UL << WS2812_EPWM_CH));

  /* 주기(틱) 기준으로 0/1 비트의 하이 구간(CMPDAT) 계산 */
  cnr      = EPWM_GET_CNR(EPWM0, WS2812_EPWM_CH) + 1UL;
  cmr_zero = (uint16_t)((cnr * WS2812_T0H_NS) / WS2812_BIT_NS);
  cmr_one  = (uint16_t)((cnr * WS2812_T1H_NS) / WS2812_BIT_NS);

  /* ---- TMR3 : 800kHz, 매 timeout 마다 PDMA 트리거 (결정적 1주기 1회 갱신) ---- */
  TIMER_Open(WS2812_TIMER, TIMER_PERIODIC_MODE, WS2812_FREQ_HZ);
  TIMER_SetTriggerSource(WS2812_TIMER, TIMER_TRGSRC_TIMEOUT_EVENT);
  TIMER_SetTriggerTarget(WS2812_TIMER, TIMER_TRG_TO_PDMA);

  /* ---- PDMA 채널 open ---- */
  PDMA_Open(PDMA, (1UL << WS2812_PDMA_CH));

  /* 초기값 off */
  for (int i = 0; i < WS2812_MAX_CH; i++)
    ws2812SetColor(i, WS2812_COLOR_OFF);

  is_init = true;
  is_busy = false;

#if CLI_USE(HW_WS2812)
  cliAdd("ws2812", cliWs2812);
#endif

  ws2812Refresh();

  return true;
}

void ws2812SetColor(uint32_t ch, uint32_t color)
{
  uint8_t   r, g, b;
  uint32_t  grb;
  uint16_t *p_bit;

  if (ch >= WS2812_MAX_CH)
    return;

  r = (color >> 16) & 0xFF;
  g = (color >>  8) & 0xFF;
  b = (color >>  0) & 0xFF;

  /* WS2812 는 G,R,B 순서, 각 바이트 MSB first */
  grb = ((uint32_t)g << 16) | ((uint32_t)r << 8) | ((uint32_t)b << 0);

  /* 데이터는 리셋 프리앰블 뒤에 배치 */
  p_bit = &cmr_buf[WS2812_RESET_LEAD + ch * WS2812_BITS_PER_LED];
  for (int i = 0; i < WS2812_BITS_PER_LED; i++)
  {
    p_bit[i] = (grb & (1UL << (23 - i))) ? cmr_one : cmr_zero;
  }
}

bool ws2812Refresh(void)
{
  if (is_init == false)
    return false;

  /* 이전 전송 완료 확인 (논블로킹) */
  if (is_busy)
  {
    if (!(PDMA_GET_TD_STS(PDMA) & (1UL << WS2812_PDMA_CH)))
      return false;   /* 아직 전송 중 -> skip */

    PDMA_CLR_TD_FLAG(PDMA, (1UL << WS2812_PDMA_CH));
    is_busy = false;
  }

  /* 타이머/EPWM 정지 후 듀티 프리로드(low) : 데이터 이전 구간을 확실히 low 로 */
  TIMER_Stop(WS2812_TIMER);
  EPWM_ForceStop(EPWM0, (1UL << WS2812_EPWM_CH));
  EPWM_SET_CMR(EPWM0, WS2812_EPWM_CH, 0);

  /* PDMA : cmr_buf -> EPWM0 CMPDAT (fixed), TMR3 timeout 트리거로 1주기당 1회 */
  PDMA_SetTransferCnt(PDMA, WS2812_PDMA_CH, PDMA_WIDTH_16, WS2812_BUF_LEN);
  PDMA_SetTransferAddr(PDMA, WS2812_PDMA_CH,
                       (uint32_t)cmr_buf, PDMA_SAR_INC,
                       (uint32_t)&EPWM0->CMPDAT[WS2812_EPWM_CH], PDMA_DAR_FIX);
  PDMA_SetTransferMode(PDMA, WS2812_PDMA_CH, WS2812_PDMA_REQ, FALSE, 0);
  PDMA_SetBurstType(PDMA, WS2812_PDMA_CH, PDMA_REQ_SINGLE, PDMA_BURST_1);
  PDMA_CLR_TD_FLAG(PDMA, (1UL << WS2812_PDMA_CH));

  /* EPWM 파형 시작 후 TMR3(트리거) 시작 : 트리거가 EPWM zero-point 뒤에 위치 */
  EPWM_Start(EPWM0, (1UL << WS2812_EPWM_CH));
  TIMER_Start(WS2812_TIMER);
  is_busy = true;

  return true;
}


#if CLI_USE(HW_WS2812)
static void cliWs2812(cli_args_t *args)
{
  bool ret = false;

  if (args->argc == 1 && args->isStr(0, "info"))
  {
    cliPrintf("ws2812 init  : %s\n", is_init ? "yes" : "no");
    cliPrintf("ws2812 led   : %d\n", WS2812_MAX_CH);
    cliPrintf("epwm ch      : %d (%s)\n", WS2812_EPWM_CH, WS2812_ON_PA4 ? "PA.4" : "PA.5");
    cliPrintf("cmr 0/1      : %d / %d\n", cmr_zero, cmr_one);
    ret = true;
  }

  if (args->argc == 2 && args->isStr(0, "test"))
  {
    uint32_t color = 0;

    if      (args->isStr(1, "red"))   color = WS2812_COLOR_RED;
    else if (args->isStr(1, "green")) color = WS2812_COLOR_GREEN;
    else if (args->isStr(1, "blue"))  color = WS2812_COLOR_BLUE;
    else if (args->isStr(1, "white")) color = WS2812_COLOR_WHITE;
    else if (args->isStr(1, "off"))   color = WS2812_COLOR_OFF;

    for (int i = 0; i < WS2812_MAX_CH; i++)
      ws2812SetColor(i, color);

    while (ws2812Refresh() == false);
    cliPrintf("set all 0x%06X\n", (int)color);
    ret = true;
  }

  if (args->argc == 5 && args->isStr(0, "set"))
  {
    uint32_t ch = (uint32_t)args->getData(1);
    uint8_t  r  = (uint8_t)args->getData(2);
    uint8_t  g  = (uint8_t)args->getData(3);
    uint8_t  b  = (uint8_t)args->getData(4);

    ws2812SetColor(ch, WS2812_COLOR(r, g, b));
    while (ws2812Refresh() == false);
    cliPrintf("ch%d = R%d G%d B%d\n", (int)ch, r, g, b);
    ret = true;
  }

  if (ret == false)
  {
    cliPrintf("ws2812 info\n");
    cliPrintf("ws2812 test [red|green|blue|white|off]\n");
    cliPrintf("ws2812 set [ch] [r] [g] [b]\n");
  }
}
#endif

#endif /* _USE_HW_WS2812 */
