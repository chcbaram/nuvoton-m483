/*
 * keys.c
 *
 *  키 매트릭스 스캐너 - EPWM1 4채널 accumulator + 순환 PDMA.
 *  [실험] COL 능동 방전(open-drain) 버전. 문제 있으면 커밋 d09125d 로 되돌릴 것.
 *
 *  회로도: ROW0~3 = PA.0~3, COL0~8 = PB.15~PB.7, COL9~11 = PB.2/PB.1/PB.0.
 *          LL4148 다이오드 anode->ROW.
 *
 *  아이디어(사용자): COL 내부 풀다운은 방전(HIGH->LOW)이 느려 settle 병목.
 *   COL 을 open-drain 출력으로 두고(그래도 PIN 으로 입력 레벨 읽힘),
 *   매 주기 COL 을 '0'으로 강제 방전 후 '1'로 릴리즈하면 눌린 키만 ROW(HIGH)로
 *   빠르게 충전됨 -> 약한 풀다운 방전 의존 제거 -> 고속화.
 *
 *  한 EPWM1 주기 = 한 행 스텝. 4채널 accumulator 를 서로 다른 포인트로 PDMA 트리거:
 *   CH0 @ zero(0%)      -> PDMA: ROW 선택 패턴을 PA->DOUT
 *   CH1 @ compare(5%)   -> PDMA: COL '0'(방전) 을 PB->DOUT  (ROW@0% 와 5% 분리)
 *   CH2 @ compare(25%)  -> PDMA: COL '1'(릴리즈) 을 PB->DOUT
 *   CH3 @ compare(75%)  -> PDMA: PB->PIN 읽기 -> col_raw (settle 후 샘플)
 *  4채널 모두 self-loop scatter-gather 로 순환 -> CPU/인터럽트 0.
 *  PA->DATMSK / PB->DATMSK 로 관련없는 비트 보호.
 *
 *  주: accumulator 는 IFACNT=0 이어야 매 주기(1×). WS2812=EPWM0/PDMA ch0 와 분리.
 */

#include "keys.h"


#ifdef _USE_HW_KEYS
#include "cli.h"


/* ROW 핀 = PA.0~3 (push-pull 출력) */
#define KEY_ROW_PORT        PA
#define KEY_ROW_MASK        (BIT0|BIT1|BIT2|BIT3)
#define KEY_ROW_PROT_MASK   (0xFFF0)            /* DATMSK: PA.4~15 보호 */

/* COL 핀 = PB.0,1,2,7~15 (open-drain 출력, PIN 으로 읽기) */
#define KEY_COL_PORT        PB
#define KEY_COL_MASK        (0xFF87)            /* PB.15~PB.7 + PB.2~PB.0 */
#define KEY_COL_PROT_MASK   (0x0078)            /* DATMSK: PB.3~6(비COL) 보호 */

#define KEY_EPWM            EPWM1

/* PDMA 채널 (WS2812=ch0 사용중이라 ch1~4) */
#define KEY_PDMA_CH_ROW     1                   /* EPWM1 CH0 @zero  : ROW 쓰기 */
#define KEY_PDMA_CH_DIS     2                   /* EPWM1 CH1 @zero  : COL 방전 */
#define KEY_PDMA_CH_REL     3                   /* EPWM1 CH2 @25%   : COL 릴리즈 */
#define KEY_PDMA_CH_READ    4                   /* EPWM1 CH3 @75%   : COL 읽기 */

#define KEY_SCAN_FREQ_HZ    500000
#define KEY_DISCHARGE_DUTY  5                   /* COL 방전 시점 % (ROW@0% 와 분리 마진) */
#define KEY_RELEASE_DUTY    25                  /* COL 릴리즈 시점 % */
#define KEY_SAMPLE_DUTY     75                  /* COL 샘플 시점 % */
#define KEY_ACC_CNT         0                   /* 0 = 매 주기(1×). 1 은 2× */


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
static uint16_t matrix[MATRIX_ROWS];

static volatile uint32_t row_pat[MATRIX_ROWS];              /* 행 선택 패턴 -> PA->DOUT */
static volatile uint32_t col_raw[MATRIX_ROWS];              /* PB->PIN 캡처 (행별) */
static volatile uint32_t col_dis_val = 0;                   /* COL 방전값(=0) */
static volatile uint32_t col_rel_val = KEY_COL_MASK;        /* COL 릴리즈값(col 비트 1) */

static __attribute__((aligned(4))) dma_desc_t desc_row;
static __attribute__((aligned(4))) dma_desc_t desc_dis;
static __attribute__((aligned(4))) dma_desc_t desc_rel;
static __attribute__((aligned(4))) dma_desc_t desc_read;

static volatile uint32_t scan_cnt = 0;                      /* 스캔 횟수 (측정 중에만 갱신) */


/* 읽기 채널 테이블 완료마다 1회 -> 스캔 횟수 카운트 ('keys rate' 실행 중에만 활성) */
void PDMA_IRQHandler(void)
{
  uint32_t status = PDMA_GET_INT_STATUS(PDMA);

  if (status & PDMA_INTSTS_TDIF_Msk)
  {
    if (PDMA_GET_TD_STS(PDMA) & (1UL << KEY_PDMA_CH_READ))
    {
      scan_cnt++;
      PDMA_CLR_TD_FLAG(PDMA, (1UL << KEY_PDMA_CH_READ));
    }
  }
}


static void keySetDesc(dma_desc_t *p_desc, uint32_t ctl_dir, uint32_t cnt,
                       uint32_t src, uint32_t dest)
{
  p_desc->ctl = ((cnt - 1) << PDMA_DSCT_CTL_TXCNT_Pos) |
                PDMA_WIDTH_32 | ctl_dir |
                PDMA_REQ_SINGLE | PDMA_TBINTDIS_DISABLE | PDMA_OP_SCATTER;
  p_desc->src    = src;
  p_desc->dest   = dest;
  p_desc->offset = (uint32_t)p_desc - (PDMA->SCATBA);        /* self-loop */
}


bool keysInit(void)
{
  for (int i = 0; i < MATRIX_ROWS; i++)
  {
    matrix[i]  = 0;
    col_raw[i] = 0;
    row_pat[i] = (1UL << row_bit[i]);
  }

  /* ---- clock ---- */
  SYS_UnlockReg();
  CLK_EnableModuleClock(EPWM1_MODULE);
  CLK_SetModuleClock(EPWM1_MODULE, CLK_CLKSEL2_EPWM1SEL_PCLK1, (uint32_t)NULL);
  CLK_EnableModuleClock(PDMA_MODULE);
  SYS_LockReg();

  /* ---- GPIO ---- */
  /* ROW : push-pull 출력, LOW, PA.4~15 보호 */
  GPIO_SetMode(KEY_ROW_PORT, KEY_ROW_MASK, GPIO_MODE_OUTPUT);
  KEY_ROW_PORT->DOUT &= ~KEY_ROW_MASK;
  GPIO_ENABLE_DOUT_MASK(KEY_ROW_PORT, KEY_ROW_PROT_MASK);

  /* COL : open-drain 출력, 유휴 릴리즈(1), 비COL 비트 보호 */
  GPIO_SetMode(KEY_COL_PORT, KEY_COL_MASK, GPIO_MODE_OPEN_DRAIN);
  KEY_COL_PORT->DOUT |= KEY_COL_MASK;
  GPIO_ENABLE_DOUT_MASK(KEY_COL_PORT, KEY_COL_PROT_MASK);

  /* ---- EPWM1 : 4채널 동일 주기, CH2=25% / CH3=75% compare ---- */
  EPWM_ConfigOutputChannel(KEY_EPWM, 0, KEY_SCAN_FREQ_HZ, 50);
  EPWM_ConfigOutputChannel(KEY_EPWM, 1, KEY_SCAN_FREQ_HZ, KEY_DISCHARGE_DUTY);
  EPWM_ConfigOutputChannel(KEY_EPWM, 2, KEY_SCAN_FREQ_HZ, KEY_RELEASE_DUTY);
  EPWM_ConfigOutputChannel(KEY_EPWM, 3, KEY_SCAN_FREQ_HZ, KEY_SAMPLE_DUTY);

  EPWM_EnableAcc(KEY_EPWM, 0, KEY_ACC_CNT, EPWM_IFA_ZERO_POINT);
  EPWM_EnableAcc(KEY_EPWM, 1, KEY_ACC_CNT, EPWM_IFA_COMPARE_UP_COUNT_POINT);
  EPWM_EnableAcc(KEY_EPWM, 2, KEY_ACC_CNT, EPWM_IFA_COMPARE_UP_COUNT_POINT);
  EPWM_EnableAcc(KEY_EPWM, 3, KEY_ACC_CNT, EPWM_IFA_COMPARE_UP_COUNT_POINT);
  EPWM_EnableAccPDMA(KEY_EPWM, 0);
  EPWM_EnableAccPDMA(KEY_EPWM, 1);
  EPWM_EnableAccPDMA(KEY_EPWM, 2);
  EPWM_EnableAccPDMA(KEY_EPWM, 3);

  /* ---- PDMA : 4채널 self-loop scatter-gather ---- */
  PDMA_Open(PDMA, (1UL << KEY_PDMA_CH_ROW) | (1UL << KEY_PDMA_CH_DIS) |
                  (1UL << KEY_PDMA_CH_REL) | (1UL << KEY_PDMA_CH_READ));

  keySetDesc(&desc_row,  PDMA_SAR_INC | PDMA_DAR_FIX, MATRIX_ROWS,
             (uint32_t)row_pat,           (uint32_t)&KEY_ROW_PORT->DOUT);
  keySetDesc(&desc_dis,  PDMA_SAR_FIX | PDMA_DAR_FIX, 1,
             (uint32_t)&col_dis_val,      (uint32_t)&KEY_COL_PORT->DOUT);
  keySetDesc(&desc_rel,  PDMA_SAR_FIX | PDMA_DAR_FIX, 1,
             (uint32_t)&col_rel_val,      (uint32_t)&KEY_COL_PORT->DOUT);
  keySetDesc(&desc_read, PDMA_SAR_FIX | PDMA_DAR_INC, MATRIX_ROWS,
             (uint32_t)&KEY_COL_PORT->PIN, (uint32_t)col_raw);

  PDMA_SetTransferMode(PDMA, KEY_PDMA_CH_ROW,  PDMA_EPWM1_CH0_TX, TRUE, (uint32_t)&desc_row);
  PDMA_SetTransferMode(PDMA, KEY_PDMA_CH_DIS,  PDMA_EPWM1_CH1_TX, TRUE, (uint32_t)&desc_dis);
  PDMA_SetTransferMode(PDMA, KEY_PDMA_CH_REL,  PDMA_EPWM1_CH2_TX, TRUE, (uint32_t)&desc_rel);
  PDMA_SetTransferMode(PDMA, KEY_PDMA_CH_READ, PDMA_EPWM1_CH3_TX, TRUE, (uint32_t)&desc_read);

  /* ---- 스캔 시작 (4채널 동시 -> 같은 위상) ---- */
  EPWM_Start(KEY_EPWM, EPWM_CH_0_MASK | EPWM_CH_1_MASK | EPWM_CH_2_MASK | EPWM_CH_3_MASK);

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
  if (is_init == false)
    return false;

  for (int r = 0; r < MATRIX_ROWS; r++)
  {
    uint32_t pb = col_raw[r];
    uint16_t cols = 0;

    for (int c = 0; c < MATRIX_COLS; c++)
    {
      if (pb & (1UL << col_bit[c]))          /* 눌림 -> 해당 COL HIGH */
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
    /* 측정 시작 : 읽기채널 테이블 인터럽트 ON */
    desc_read.ctl &= ~PDMA_DSCT_CTL_TBINTDIS_Msk;
    PDMA_CLR_TD_FLAG(PDMA, (1UL << KEY_PDMA_CH_READ));
    PDMA_EnableInt(PDMA, KEY_PDMA_CH_READ, PDMA_INT_TRANS_DONE);
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
      uint32_t step_hz = scan_hz * MATRIX_ROWS;

      cliPrintf("scan %d Hz, step %d Hz (set %d Hz) -> %s\n",
                (int)scan_hz, (int)step_hz, (int)KEY_SCAN_FREQ_HZ,
                (step_hz > (uint32_t)(KEY_SCAN_FREQ_HZ * 3 / 4)) ? "1x(normal)" : "2x?");
    }

    /* 측정 종료 : 인터럽트 OFF 원복 */
    NVIC_DisableIRQ(PDMA_IRQn);
    PDMA_DisableInt(PDMA, KEY_PDMA_CH_READ, PDMA_INT_TRANS_DONE);
    desc_read.ctl |= PDMA_DSCT_CTL_TBINTDIS_Msk;
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
