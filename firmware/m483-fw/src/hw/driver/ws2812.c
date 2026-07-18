/*
 * ws2812.c
 *
 *  WS2812(NeoPixel) 드라이버 - EPWM0 + TMR3 + PDMA "연속 순환(circular)" 방식.
 *
 *  회로도: MCU NEOPIXEL_3V3(PA.5) -> TXB0101 레벨시프터(3V3->5V) ->
 *          SK6812MINI-E 45개 직렬 체인(GRB).
 *
 *  [설계 - 오리지널 QMK ws2812_pwm 방식]
 *   - EPWM0 를 800kHz(1.25us) 로 연속 구동해 WS2812 비트 파형을 만든다.
 *   - TMR3(800kHz)가 매 주기 PDMA 를 1회 트리거해 비트 듀티(CMPDAT)를 갱신한다.
 *   - PDMA 는 self-loop scatter-gather(순환) 로 프레임버퍼를 **끊김 없이 반복** 전송한다.
 *     프레임버퍼 = [reset(0)][color bits][reset(0)] 이며, reset 구간(low)이 매 loop 사이의
 *     latch 를 만든다. => 매 프레임 EPWM/TMR/PDMA 를 정지·재시작하지 않는다(재시작 글리치 제거).
 *   - 색 갱신: 프레임버퍼를 **in-place** 로 빠르게 다시 쓴다(더블버퍼 없이; QMK 와 동일).
 *     순환 DMA 가 다음 loop 부터 새 색을 반영한다. (tear 창 ~10us, 무시 수준)
 *   - init 에서 1회 시작 후 정지하지 않는다. RGB '꺼짐' 은 버퍼에 black 을 써서 스트림한다.
 */

#include "ws2812.h"


#ifdef _USE_HW_WS2812
#include "cli.h"
#include <string.h>


/* NEOPIXEL_3V3 핀 = PA.5(EPWM0_CH0). PA.4(CH1) 로 바꾸려면 WS2812_ON_PA4=1. */
#define WS2812_ON_PA4           0

#if WS2812_ON_PA4
  #define WS2812_EPWM_CH        1
#else
  #define WS2812_EPWM_CH        0
#endif

#define WS2812_TIMER            TIMER3
#define WS2812_PDMA_REQ         PDMA_TMR3
#define WS2812_PDMA_CH          0             /* WS2812 전용 PDMA 채널 (매트릭스=ch1~4) */

#define WS2812_FREQ_HZ          800000
#define WS2812_T0H_NS           350
#define WS2812_T1H_NS           700
#define WS2812_BIT_NS           1250

/* reset(low) 구간(0 듀티). loop 사이 latch 용. 1.25us*90=112.5us(SK6812 >80us 충족). */
#define WS2812_RESET_LEAD       90
#define WS2812_RESET_TAIL       90

#define WS2812_BITS_PER_LED     24
#define WS2812_BUF_LEN          (WS2812_RESET_LEAD + WS2812_MAX_CH * WS2812_BITS_PER_LED + WS2812_RESET_TAIL)


typedef struct
{
  uint32_t ctl;
  uint32_t src;
  uint32_t dest;
  uint32_t offset;
} dma_desc_t;


static bool     is_init    = false;
static bool     is_running = false;

static uint32_t led_color[WS2812_MAX_CH];  /* LED 색 (0x00RRGGBB) — set_color 가 여기만 쓴다 */
static uint16_t cmr_buf[WS2812_BUF_LEN];    /* 비트별 EPWM CMPDAT(듀티) — 순환 DMA 가 계속 읽는다 */
static uint16_t cmr_zero = 0;
static uint16_t cmr_one  = 0;

static __attribute__((aligned(4))) dma_desc_t desc;

#if CLI_USE(HW_WS2812)
static void cliWs2812(cli_args_t *args);
#endif


/* led_color[] -> cmr_buf 비트 전개 (in-place). 순환 DMA 가 다음 loop 부터 반영. */
static void ws2812Build(void)
{
  for (uint32_t ch = 0; ch < WS2812_MAX_CH; ch++)
  {
    uint8_t   r = (led_color[ch] >> 16) & 0xFF;
    uint8_t   g = (led_color[ch] >>  8) & 0xFF;
    uint8_t   b = (led_color[ch] >>  0) & 0xFF;
    uint32_t  grb = ((uint32_t)g << 16) | ((uint32_t)r << 8) | ((uint32_t)b << 0);  /* GRB, MSB first */
    uint16_t *p_bit = &cmr_buf[WS2812_RESET_LEAD + ch * WS2812_BITS_PER_LED];

    for (int i = 0; i < WS2812_BITS_PER_LED; i++)
      p_bit[i] = (grb & (1UL << (23 - i))) ? cmr_one : cmr_zero;
  }
}


bool ws2812Init(void)
{
  uint32_t cnr;

  memset(cmr_buf,   0, sizeof(cmr_buf));    /* reset 구간 포함 전체 low(0) 로 시작 */
  memset(led_color, 0, sizeof(led_color));

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

  /* ---- EPWM 800kHz ---- */
  EPWM_ConfigOutputChannel(EPWM0, WS2812_EPWM_CH, WS2812_FREQ_HZ, 30);
  EPWM_EnableOutput(EPWM0, (1UL << WS2812_EPWM_CH));

  cnr      = EPWM_GET_CNR(EPWM0, WS2812_EPWM_CH) + 1UL;
  cmr_zero = (uint16_t)((cnr * WS2812_T0H_NS) / WS2812_BIT_NS);
  cmr_one  = (uint16_t)((cnr * WS2812_T1H_NS) / WS2812_BIT_NS);

  /* 초기 off 프레임 전개 */
  ws2812Build();

  /* ---- TMR3 : 800kHz, 매 timeout 마다 PDMA 트리거 ---- */
  TIMER_Open(WS2812_TIMER, TIMER_PERIODIC_MODE, WS2812_FREQ_HZ);
  TIMER_SetTriggerSource(WS2812_TIMER, TIMER_TRGSRC_TIMEOUT_EVENT);
  TIMER_SetTriggerTarget(WS2812_TIMER, TIMER_TRG_TO_PDMA);

  /* ---- PDMA : self-loop scatter-gather(순환) 디스크립터 ---- */
  PDMA_Open(PDMA, (1UL << WS2812_PDMA_CH));

  desc.ctl = ((WS2812_BUF_LEN - 1) << PDMA_DSCT_CTL_TXCNT_Pos) |
             PDMA_WIDTH_16 | PDMA_SAR_INC | PDMA_DAR_FIX |
             PDMA_REQ_SINGLE | PDMA_TBINTDIS_DISABLE | PDMA_OP_SCATTER;
  desc.src    = (uint32_t)cmr_buf;
  desc.dest   = (uint32_t)&EPWM0->CMPDAT[WS2812_EPWM_CH];
  desc.offset = (uint32_t)&desc - (PDMA->SCATBA);      /* self-loop */

  PDMA_SetTransferMode(PDMA, WS2812_PDMA_CH, WS2812_PDMA_REQ, TRUE, (uint32_t)&desc);

  /* ---- 연속 시작 (이후 정지/재시작 없음) ---- */
  EPWM_Start(EPWM0, (1UL << WS2812_EPWM_CH));
  TIMER_Start(WS2812_TIMER);

  is_init    = true;
  is_running = true;

#if CLI_USE(HW_WS2812)
  cliAdd("ws2812", cliWs2812);
#endif

  return true;
}

void ws2812SetColor(uint32_t ch, uint32_t color)
{
  if (ch >= WS2812_MAX_CH)
    return;

  led_color[ch] = color;
}

/*
 * 색을 프레임버퍼에 반영(in-place). 순환 DMA 가 다음 loop 부터 새 색을 전송한다.
 * 정지/재시작이 없으므로 항상 true. (rgb_matrix flush 가 프레임마다 1회 호출)
 */
bool ws2812Refresh(void)
{
  if (is_init == false)
    return false;

  ws2812Build();
  return true;
}

/* 연속 순환 방식에선 별도 재시도가 불필요(항상 스트림 중). 호환용 no-op. */
void ws2812Poll(void)
{
}


#if CLI_USE(HW_WS2812)
static void cliWs2812(cli_args_t *args)
{
  bool ret = false;

  if (args->argc == 1 && args->isStr(0, "info"))
  {
    cliPrintf("ws2812 init  : %s\n", is_init ? "yes" : "no");
    cliPrintf("ws2812 run   : %s (continuous circular DMA)\n", is_running ? "yes" : "no");
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
    ws2812Refresh();

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
    ws2812Refresh();
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
