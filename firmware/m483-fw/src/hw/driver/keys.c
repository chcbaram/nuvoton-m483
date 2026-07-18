/*
 * keys.c
 *
 *  키 매트릭스 스캐너 - EPWM1 4채널 accumulator + 순환 PDMA + COL 능동 방전(open-drain).
 *  방전/풀다운은 KEY_USE_DISCHARGE / KEY_USE_PULLDOWN 로 토글. 실측 1MHz 동작.
 *
 *  [보드별 파라미터화]
 *   포트는 고정 : ROW = PA (push-pull 출력), COL = PB (open-drain, PIN 읽기).
 *   ROW/COL 개수와 포트 내 핀 비트순서는 보드마다 다르며 keysInit(cfg) 로 주입한다.
 *   마스크(ROW/COL/보호)는 주입된 비트 배열로 init 시 1회 계산 -> 핫패스 비용 불변.
 *
 *  한 EPWM1 주기 = 한 행 스텝. 4채널 accumulator 를 서로 다른 포인트로 PDMA 트리거:
 *   CH0 @ zero(0%)      -> PDMA: ROW 선택 패턴을 PA->DOUT
 *   CH1 @ compare(5%)   -> PDMA: COL '0'(방전) 을 PB->DOUT
 *   CH2 @ compare(25%)  -> PDMA: COL '1'(릴리즈) 을 PB->DOUT
 *   CH3 @ compare(75%)  -> PDMA: PB->PIN 읽기 -> col_raw (settle 후 샘플)
 *  4채널 모두 self-loop scatter-gather 로 순환 -> CPU/인터럽트 0.
 *
 *  주: accumulator 는 IFACNT=0 이어야 매 주기(1×). WS2812=EPWM0/PDMA ch0 와 분리.
 */

#include "keys.h"


#ifdef _USE_HW_KEYS
#include "cli.h"
#include <string.h>


/* ROW 핀 포트 = PA (push-pull 출력) / COL 핀 포트 = PB (open-drain) — 고정 */
#define KEY_ROW_PORT        PA
#define KEY_COL_PORT        PB

#define KEY_EPWM            EPWM1

/* PDMA 채널 (WS2812=ch0 사용중이라 ch1~4) */
#define KEY_PDMA_CH_ROW     1                   /* EPWM1 CH0 @zero  : ROW 쓰기 */
#define KEY_PDMA_CH_DIS     2                   /* EPWM1 CH1 @5%    : COL 방전 */
#define KEY_PDMA_CH_REL     3                   /* EPWM1 CH2 @25%   : COL 릴리즈 */
#define KEY_PDMA_CH_READ    4                   /* EPWM1 CH3 @75%   : COL 읽기 */

#define KEY_SCAN_FREQ_HZ    1000000
#define KEY_DISCHARGE_DUTY  5                   /* COL 방전 시점 % */
#define KEY_RELEASE_DUTY    55                  /* COL 릴리즈 시점 % */
#define KEY_SAMPLE_DUTY     95                  /* COL 샘플 시점 % */
#define KEY_ACC_CNT         0                   /* 0 = 매 주기(1×). 1 은 2× */
#define KEY_USE_DISCHARGE   1                   /* 1=능동방전 */
#define KEY_USE_PULLDOWN    1                   /* 1=COL 내부 풀다운 병행(보험) */


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


/* ---- 보드별 런타임 설정 (keysInit(cfg) 에서 채움) ---- */
static uint8_t  col_bit[KEY_COL_MAX];      /* COL 이 매핑된 PB 비트번호 */
static uint8_t  row_bit[KEY_ROW_MAX];      /* ROW 가 매핑된 PA 비트번호 */
static uint8_t  key_row_cnt = 0;
static uint8_t  key_col_cnt = 0;

static uint32_t key_row_mask = 0;          /* OR(1<<row_bit) */
static uint32_t key_col_mask = 0;          /* OR(1<<col_bit) */
static uint32_t key_row_prot = 0;          /* DATMSK: ROW 외 비트 보호 = ~row_mask (16b) */
static uint32_t key_col_prot = 0;          /* DATMSK: COL 외 비트 보호 = ~col_mask (16b) */

static bool     is_init = false;
static uint16_t matrix[KEY_ROW_MAX];

static volatile uint32_t row_pat[KEY_ROW_MAX];             /* 행 선택 패턴 -> PA->DOUT */
static volatile uint32_t col_raw[KEY_ROW_MAX];             /* PB->PIN 캡처 (행별) */
static volatile uint32_t col_dis_val = 0;                  /* 방전값 */
static volatile uint32_t col_rel_val = 0;                  /* 릴리즈값 (col 비트 1) */

static __attribute__((aligned(4))) dma_desc_t desc_row;
static __attribute__((aligned(4))) dma_desc_t desc_dis;
static __attribute__((aligned(4))) dma_desc_t desc_rel;
static __attribute__((aligned(4))) dma_desc_t desc_read;

static volatile uint32_t scan_cnt = 0;                     /* 스캔 횟수 (측정 중에만 갱신) */


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


bool keysInit(const keys_cfg_t *cfg)
{
  if (cfg == NULL || cfg->row_bits == NULL || cfg->col_bits == NULL)
    return false;
  if (cfg->row_cnt == 0 || cfg->row_cnt > KEY_ROW_MAX ||
      cfg->col_cnt == 0 || cfg->col_cnt > KEY_COL_MAX)
    return false;

  key_row_cnt = cfg->row_cnt;
  key_col_cnt = cfg->col_cnt;

  /* 핀맵 복사 + 마스크 계산 (init 1회) */
  key_row_mask = 0;
  key_col_mask = 0;
  for (int i = 0; i < key_row_cnt; i++)
  {
    row_bit[i]    = cfg->row_bits[i];
    row_pat[i]    = (1UL << row_bit[i]);
    key_row_mask |= (1UL << row_bit[i]);
    matrix[i]     = 0;
    col_raw[i]    = 0;
  }
  for (int i = 0; i < key_col_cnt; i++)
  {
    col_bit[i]    = cfg->col_bits[i];
    key_col_mask |= (1UL << col_bit[i]);
  }
  key_row_prot = (~key_row_mask) & 0xFFFF;   /* DATMSK: ROW 외 비트 보호 */
  key_col_prot = (~key_col_mask) & 0xFFFF;   /* DATMSK: COL 외 비트 보호 */

  col_dis_val = KEY_USE_DISCHARGE ? 0 : key_col_mask;
  col_rel_val = key_col_mask;

  /* ---- clock ---- */
  SYS_UnlockReg();
  CLK_EnableModuleClock(EPWM1_MODULE);
  CLK_SetModuleClock(EPWM1_MODULE, CLK_CLKSEL2_EPWM1SEL_PCLK1, (uint32_t)NULL);
  CLK_EnableModuleClock(PDMA_MODULE);
  SYS_LockReg();

  /* ---- GPIO ---- */
  /* ROW : push-pull 출력, LOW, ROW 외 보호 */
  GPIO_SetMode(KEY_ROW_PORT, key_row_mask, GPIO_MODE_OUTPUT);
  KEY_ROW_PORT->DOUT &= ~key_row_mask;
  GPIO_ENABLE_DOUT_MASK(KEY_ROW_PORT, key_row_prot);

  /* COL : open-drain 출력, 유휴 릴리즈(1), COL 외 보호 */
  GPIO_SetMode(KEY_COL_PORT, key_col_mask, GPIO_MODE_OPEN_DRAIN);
  KEY_COL_PORT->DOUT |= key_col_mask;
#if KEY_USE_PULLDOWN
  GPIO_SetPullCtl(KEY_COL_PORT, key_col_mask, GPIO_PUSEL_PULL_DOWN);
#endif
  GPIO_ENABLE_DOUT_MASK(KEY_COL_PORT, key_col_prot);

  /* ---- EPWM1 : 4채널 동일 주기 ---- */
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

  keySetDesc(&desc_row,  PDMA_SAR_INC | PDMA_DAR_FIX, key_row_cnt,
             (uint32_t)row_pat,            (uint32_t)&KEY_ROW_PORT->DOUT);
  keySetDesc(&desc_dis,  PDMA_SAR_FIX | PDMA_DAR_FIX, 1,
             (uint32_t)&col_dis_val,       (uint32_t)&KEY_COL_PORT->DOUT);
  keySetDesc(&desc_rel,  PDMA_SAR_FIX | PDMA_DAR_FIX, 1,
             (uint32_t)&col_rel_val,       (uint32_t)&KEY_COL_PORT->DOUT);
  keySetDesc(&desc_read, PDMA_SAR_FIX | PDMA_DAR_INC, key_row_cnt,
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

  for (int r = 0; r < key_row_cnt; r++)
  {
    uint32_t pb = col_raw[r];
    uint16_t cols = 0;

    for (int c = 0; c < key_col_cnt; c++)
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
  if (row >= key_row_cnt || col >= key_col_cnt)
    return false;

  return (matrix[row] & (1UL << col)) ? true : false;
}

uint16_t keysGetRow(uint16_t row)
{
  return (row < key_row_cnt) ? matrix[row] : 0;
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
      for (int cols = 0; cols < key_col_cnt; cols++)
        cliPrintf("%02d ", cols);
      cliPrintf("\n");

      for (int rows = 0; rows < key_row_cnt; rows++)
      {
        cliPrintf("%02d : ", rows);
        for (int cols = 0; cols < key_col_cnt; cols++)
          cliPrintf("%s  ", keysGetPressed(rows, cols) ? "O" : "_");
        cliPrintf("\n");
      }
      cliMoveUp(key_row_cnt + 1);
    }
    cliMoveDown(key_row_cnt + 1);

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
      uint32_t step_hz = scan_hz * key_row_cnt;

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
