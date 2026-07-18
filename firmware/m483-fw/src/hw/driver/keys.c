/*
 * keys.c
 *
 *  키 매트릭스 스캐너 (단순 GPIO 4-step 스캔, 논블로킹 호출).
 *
 *  회로도(SCH_MCU / SCH_BARAM-45 PCB):
 *   - ROW0~3 = PA.0~3
 *   - COL0~8 = PB.15~PB.7, COL9~11 = PB.2/PB.1/PB.0. 키마다 LL4148 다이오드(anode->ROW).
 *   - 다이오드 anode->ROW 이므로 ROW 를 구동(select)하고 COL 을 읽는 방식이 가능하며,
 *     ROW(4) 를 구동하므로 COL 구동(12) 대비 스캔 단계가 1/3.
 *   - 극성: 선택 ROW = HIGH(나머지 LOW), COL = 입력+내부 풀다운.
 *           눌린 키 -> 해당 COL 이 HIGH 로 끌림 -> COL 비트 1 = 눌림.
 *
 *  baram-qmk 의 keys API(keysInit/keysUpdate/keysGetPressed) 미러링.
 *  주: 향후 Timer+PDMA→GPIO 로 최적화 예정(현재는 CPU 루프).
 */

#include "keys.h"


#ifdef _USE_HW_KEYS
#include "cli.h"


/* ROW 선택 후 COL 라인이 안정될 때까지의 지연(us). 내부 풀다운 방전 시간 고려. */
#define KEY_SETTLE_US       10

/* ROW 핀 = PA.0~3 (연속) */
#define KEY_ROW_PORT        PA
#define KEY_ROW_PORT_IDX    0                 /* GPIO_PIN_DATA 용 PA=0 */
#define KEY_ROW_MASK        (BIT0|BIT1|BIT2|BIT3)

/* COL 핀 = PB.0,1,2,7,8,9,10,11,12,13,14,15 */
#define KEY_COL_PORT        PB
#define KEY_COL_MASK        (0xFF87)          /* PB.15~PB.7 + PB.2~PB.0 */

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
static uint16_t matrix[MATRIX_ROWS];          /* matrix[row] 의 bit c = COL c 눌림 */


static inline void keyDelayUs(uint32_t us)
{
  uint32_t t = micros();
  while ((micros() - t) < us);
}


bool keysInit(void)
{
  for (int i = 0; i < MATRIX_ROWS; i++)
    matrix[i] = 0;

  /* ROW : 출력, 초기 LOW */
  GPIO_SetMode(KEY_ROW_PORT, KEY_ROW_MASK, GPIO_MODE_OUTPUT);
  KEY_ROW_PORT->DOUT &= ~KEY_ROW_MASK;

  /* COL : 입력 + 내부 풀다운 */
  GPIO_SetMode(KEY_COL_PORT, KEY_COL_MASK, GPIO_MODE_INPUT);
  GPIO_SetPullCtl(KEY_COL_PORT, KEY_COL_MASK, GPIO_PUSEL_PULL_DOWN);

  is_init = true;

#if CLI_USE(HW_KEYS)
  cliAdd("keys", cliKeys);
#endif

  return true;
}

bool keysIsBusy(void)
{
  return false;   /* 동기 스캔 */
}

bool keysUpdate(void)
{
  if (is_init == false)
    return false;

  for (int r = 0; r < MATRIX_ROWS; r++)
  {
    uint32_t pb;
    uint16_t cols = 0;

    /* 선택 ROW 만 HIGH, 나머지 LOW (PA.0~3 외 비트는 보존) */
    KEY_ROW_PORT->DOUT = (KEY_ROW_PORT->DOUT & ~KEY_ROW_MASK) | (1UL << row_bit[r]);

    keyDelayUs(KEY_SETTLE_US);

    pb = KEY_COL_PORT->PIN;

    for (int c = 0; c < MATRIX_COLS; c++)
    {
      if (pb & (1UL << col_bit[c]))
        cols |= (1UL << c);
    }
    matrix[r] = cols;
  }

  /* 스캔 종료 후 ROW 전부 LOW */
  KEY_ROW_PORT->DOUT &= ~KEY_ROW_MASK;

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

  if (ret == false)
  {
    cliPrintf("keys info\n");
  }
}
#endif

#endif /* _USE_HW_KEYS */
