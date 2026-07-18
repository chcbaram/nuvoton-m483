/*
 * keys.c
 *
 *  키 매트릭스 스캐너 - EPWM1 accumulator + 순환 PDMA (CPU·인터럽트 0, 백그라운드 스캔).
 *
 *  회로도(SCH_MCU / SCH_BARAM-45 PCB):
 *   - ROW0~3 = PA.0~3, COL0~8 = PB.15~PB.7, COL9~11 = PB.2/PB.1/PB.0.
 *   - LL4148 다이오드 anode->ROW. 선택 ROW=HIGH, COL=입력+내부 풀다운, 눌림 -> COL=1.
 *
 *  방식 (blog: chcbaram "Nuvoton M483 고속 키스캔"):
 *   - EPWM1 한 카운터에서 두 채널의 accumulator 를 서로 다른 포인트로 PDMA 트리거.
 *     CH0 @ zero point (0%)     -> PDMA(W): row_pat[i] 를 PA->DOUT 에 쓰기 (행 선택)
 *     CH1 @ compare-up (75%)    -> PDMA(R): PB->PIN 을 col_raw[i] 로 읽기 (settle 후 열 샘플)
 *   - 두 PDMA 채널 모두 self-loop scatter-gather 로 4행을 무한 순환 -> CPU/인터럽트 없이
 *     매트릭스가 백그라운드로 계속 갱신됨. keysUpdate() 는 col_raw 를 디코드만 한다.
 *   - PA->DATMSK 로 PA.4~15(WS2812/I2C) 를 보호하고 PA.0~3 만 갱신.
 *   - WS2812 가 EPWM0/TMR3, PDMA ch0 을 쓰므로 매트릭스는 EPWM1, PDMA ch1/ch2 사용.
 *
 *  baram-qmk 의 keys API 미러링.
 */

#include "keys.h"


#ifdef _USE_HW_KEYS
#include "cli.h"


/* ROW 핀 = PA.0~3 */
#define KEY_ROW_PORT        PA
#define KEY_ROW_MASK        (BIT0|BIT1|BIT2|BIT3)
#define KEY_ROW_PROT_MASK   (0xFFF0)            /* DATMSK: PA.4~15 보호 (PA.0~3 만 쓰기) */

/* COL 핀 = PB.0,1,2,7~15 */
#define KEY_COL_PORT        PB
#define KEY_COL_MASK        (0xFF87)            /* PB.15~PB.7 + PB.2~PB.0 */

/* EPWM / PDMA 자원 */
#define KEY_EPWM            EPWM1
#define KEY_PDMA_CH_W       1                   /* 행 쓰기 (EPWM1 CH0 accumulator) */
#define KEY_PDMA_CH_R       2                   /* 열 읽기 (EPWM1 CH1 accumulator) */

/* 스캔 스텝 주파수(=EPWM 주기). 100kHz -> 10us/스텝, 4행 -> 40us/스캔(25kHz). */
#define KEY_SCAN_FREQ_HZ    200000
#define KEY_SAMPLE_DUTY     75                  /* 열 샘플 시점 % (settle = 75%*주기) */

/*
 * accumulator 트리거 카운트. 실측 확인: IFACNT=1 은 2주기마다 트리거(2×),
 * **IFACNT=0 이어야 매 주기(1×)**. (PDMA ack 핸드셰이크가 1주기 추가되기 때문)
 */
#define KEY_ACC_CNT         0


typedef struct
{
  uint32_t ctl;
  uint32_t src;
  uint32_t dest;
  uint32_t offset;
} dma_desc_t;


#if CLI_USE(HW_KEYS)
static void cliKeys(cli_args_t *args);
#endif


/* COL0~11 이 매핑된 PB 비트번호 : COL0~8=PB.15~PB.7, COL9~11=PB.2/PB.1/PB.0 */
static const uint8_t col_bit[MATRIX_COLS] =
{
  15, 14, 13, 12, 11, 10, 9, 8, 7, 2, 1, 0
};

/* ROW0~3 이 매핑된 PA 비트번호 */
static const uint8_t row_bit[MATRIX_ROWS] =
{
  0, 1, 2, 3
};

static bool     is_init = false;
static uint16_t matrix[MATRIX_ROWS];                        /* matrix[row] bit c = COL c 눌림 */

static volatile uint32_t row_pat[MATRIX_ROWS];              /* 행 선택 패턴 (PA->DOUT 로 전송) */
static volatile uint32_t col_raw[MATRIX_ROWS];              /* PB->PIN 캡처 (행별) */

static __attribute__((aligned(4))) dma_desc_t desc_w;       /* 행 쓰기 SG (self-loop) */
static __attribute__((aligned(4))) dma_desc_t desc_r;       /* 열 읽기 SG (self-loop) */

static volatile uint32_t scan_cnt = 0;                      /* 완료된 스캔 횟수 (측정 중에만 갱신) */


/*
 * 읽기 채널 SG 테이블 완료마다 1회 -> 스캔 횟수 카운트.
 * 평소엔 desc_r 의 테이블 인터럽트가 꺼져 있어 호출되지 않고, 'keys rate' 실행 중에만 활성.
 */
void PDMA_IRQHandler(void)
{
  uint32_t status = PDMA_GET_INT_STATUS(PDMA);

  if (status & PDMA_INTSTS_TDIF_Msk)
  {
    if (PDMA_GET_TD_STS(PDMA) & (1UL << KEY_PDMA_CH_R))
    {
      scan_cnt++;
      PDMA_CLR_TD_FLAG(PDMA, (1UL << KEY_PDMA_CH_R));
    }
  }
}


bool keysInit(void)
{
  for (int i = 0; i < MATRIX_ROWS; i++)
  {
    matrix[i]  = 0;
    col_raw[i] = 0;
    row_pat[i] = (1UL << row_bit[i]);                       /* 선택 행만 HIGH */
  }

  /* ---- clock ---- */
  SYS_UnlockReg();
  CLK_EnableModuleClock(EPWM1_MODULE);
  CLK_SetModuleClock(EPWM1_MODULE, CLK_CLKSEL2_EPWM1SEL_PCLK1, (uint32_t)NULL);
  CLK_EnableModuleClock(PDMA_MODULE);
  SYS_LockReg();

  /* ---- GPIO ---- */
  /* ROW : 출력, 초기 LOW, PA.4~15 는 DATMSK 로 보호(PDMA 가 PA.0~3 만 갱신) */
  GPIO_SetMode(KEY_ROW_PORT, KEY_ROW_MASK, GPIO_MODE_OUTPUT);
  KEY_ROW_PORT->DOUT &= ~KEY_ROW_MASK;
  GPIO_ENABLE_DOUT_MASK(KEY_ROW_PORT, KEY_ROW_PROT_MASK);

  /* COL : 입력 + 내부 풀다운 */
  GPIO_SetMode(KEY_COL_PORT, KEY_COL_MASK, GPIO_MODE_INPUT);
  GPIO_SetPullCtl(KEY_COL_PORT, KEY_COL_MASK, GPIO_PUSEL_PULL_DOWN);

  /* ---- EPWM1 : CH0/CH1 동일 주기, CH1 CMR = 75% ---- */
  EPWM_ConfigOutputChannel(KEY_EPWM, 0, KEY_SCAN_FREQ_HZ, 50);
  EPWM_ConfigOutputChannel(KEY_EPWM, 1, KEY_SCAN_FREQ_HZ, KEY_SAMPLE_DUTY);

  /* accumulator : CH0=zero(0%) -> PDMA(W), CH1=compare-up(75%) -> PDMA(R) */
  EPWM_EnableAcc(KEY_EPWM, 0, KEY_ACC_CNT, EPWM_IFA_ZERO_POINT);
  EPWM_EnableAccPDMA(KEY_EPWM, 0);
  EPWM_EnableAcc(KEY_EPWM, 1, KEY_ACC_CNT, EPWM_IFA_COMPARE_UP_COUNT_POINT);
  EPWM_EnableAccPDMA(KEY_EPWM, 1);

  /* ---- PDMA : 두 채널 self-loop scatter-gather (순환) ---- */
  PDMA_Open(PDMA, (1UL << KEY_PDMA_CH_W) | (1UL << KEY_PDMA_CH_R));

  /* W: row_pat[] -> PA->DOUT, 4행 순환 */
  desc_w.ctl = ((MATRIX_ROWS - 1) << PDMA_DSCT_CTL_TXCNT_Pos) |
               PDMA_WIDTH_32 | PDMA_SAR_INC | PDMA_DAR_FIX |
               PDMA_REQ_SINGLE | PDMA_TBINTDIS_DISABLE | PDMA_OP_SCATTER;
  desc_w.src    = (uint32_t)row_pat;
  desc_w.dest   = (uint32_t)&KEY_ROW_PORT->DOUT;
  desc_w.offset = (uint32_t)&desc_w - (PDMA->SCATBA);        /* self-loop */

  /* R: PB->PIN -> col_raw[], 4행 순환 (인터럽트 없음, TD 폴링도 불필요) */
  desc_r.ctl = ((MATRIX_ROWS - 1) << PDMA_DSCT_CTL_TXCNT_Pos) |
               PDMA_WIDTH_32 | PDMA_SAR_FIX | PDMA_DAR_INC |
               PDMA_REQ_SINGLE | PDMA_TBINTDIS_DISABLE | PDMA_OP_SCATTER;
  desc_r.src    = (uint32_t)&KEY_COL_PORT->PIN;
  desc_r.dest   = (uint32_t)col_raw;
  desc_r.offset = (uint32_t)&desc_r - (PDMA->SCATBA);        /* self-loop */

  PDMA_SetTransferMode(PDMA, KEY_PDMA_CH_W, PDMA_EPWM1_CH0_TX, TRUE, (uint32_t)&desc_w);
  PDMA_SetTransferMode(PDMA, KEY_PDMA_CH_R, PDMA_EPWM1_CH1_TX, TRUE, (uint32_t)&desc_r);

  /* ---- 스캔 시작 (두 채널 동시 -> 같은 카운터 위상) ---- */
  EPWM_Start(KEY_EPWM, EPWM_CH_0_MASK | EPWM_CH_1_MASK);

  is_init = true;

#if CLI_USE(HW_KEYS)
  cliAdd("keys", cliKeys);
#endif

  return true;
}

bool keysIsBusy(void)
{
  return false;
}

bool keysUpdate(void)
{
  /* 스캔은 PDMA 가 백그라운드로 계속 수행. 여기서는 최신 캡처를 디코드만 한다. */
  if (is_init == false)
    return false;

  for (int r = 0; r < MATRIX_ROWS; r++)
  {
    uint32_t pb = col_raw[r];
    uint16_t cols = 0;

    for (int c = 0; c < MATRIX_COLS; c++)
    {
      if (pb & (1UL << col_bit[c]))
        cols |= (1UL << c);
    }
    matrix[r] = cols;
  }

  return true;
}

bool keysGetPressed(uint16_t row, uint16_t col)
{
  if (row >= MATRIX_ROWS || col >= MATRIX_COLS)
    return false;

  return (matrix[row] & (1UL << col)) ? true : false;
}


#if CLI_USE(HW_KEYS)
static void cliKeys(cli_args_t *args)
{
  bool ret = false;

  if (args->argc == 1 && args->isStr(0, "info"))
  {
    cliShowCursor(false);

    while (cliKeepLoop())
    {
      keysUpdate();
      delay(10);

      cliPrintf("     ");
      for (int cols = 0; cols < MATRIX_COLS; cols++)
      {
        cliPrintf("%02d ", cols);
      }
      cliPrintf("\n");

      for (int rows = 0; rows < MATRIX_ROWS; rows++)
      {
        cliPrintf("%02d : ", rows);

        for (int cols = 0; cols < MATRIX_COLS; cols++)
        {
          if (keysGetPressed(rows, cols))
            cliPrintf("O  ");
          else
            cliPrintf("_  ");
        }
        cliPrintf("\n");
      }
      cliMoveUp(MATRIX_ROWS + 1);
    }
    cliMoveDown(MATRIX_ROWS + 1);

    cliShowCursor(true);
    ret = true;
  }

  if (args->argc == 1 && args->isStr(0, "rate"))
  {
    /* 측정 시작 : 읽기채널 테이블 인터럽트 ON (self-loop 다음 reload 부터 반영) */
    desc_r.ctl &= ~PDMA_DSCT_CTL_TBINTDIS_Msk;
    PDMA_CLR_TD_FLAG(PDMA, (1UL << KEY_PDMA_CH_R));
    PDMA_EnableInt(PDMA, KEY_PDMA_CH_R, PDMA_INT_TRANS_DONE);
    NVIC_EnableIRQ(PDMA_IRQn);

    cliPrintf("scan rate 측정 (Ctrl-C 종료)\n");
    while (cliKeepLoop())
    {
      uint32_t t0 = scan_cnt;
      uint32_t m0 = millis();
      delay(1000);
      uint32_t scans = scan_cnt - t0;
      uint32_t ms    = millis() - m0;

      uint32_t scan_hz = (ms > 0) ? (scans * 1000UL / ms) : 0;
      uint32_t step_hz = scan_hz * MATRIX_ROWS;    /* 스텝(행)당 실제 트리거 주파수 */

      cliPrintf("scan %d Hz, step %d Hz (set %d Hz) -> %s\n",
                (int)scan_hz, (int)step_hz, (int)KEY_SCAN_FREQ_HZ,
                (step_hz > (uint32_t)(KEY_SCAN_FREQ_HZ * 3 / 4)) ? "1x(normal)" : "2x?");
    }

    /* 측정 종료 : 인터럽트 OFF 로 원복 (평소 인터럽트 0) */
    NVIC_DisableIRQ(PDMA_IRQn);
    PDMA_DisableInt(PDMA, KEY_PDMA_CH_R, PDMA_INT_TRANS_DONE);
    desc_r.ctl |= PDMA_DSCT_CTL_TBINTDIS_Msk;
    ret = true;
  }

  if (ret == false)
  {
    cliPrintf("keys info\n");
    cliPrintf("keys rate\n");
  }
}
#endif

#endif /* _USE_HW_KEYS */
