/*
 * 부트키 매트릭스 : GPIO 폴링 전용 (앱의 8K 엔진과 달리 PDMA/EPWM 미사용).
 * ROW = PA 푸시풀, 한 번에 한 행만 HIGH 구동 / COL = PB 입력 + 풀다운.
 * 눌린 키는 컬럼을 HIGH로 끌어올림 -> active-high 로 읽음. 컬럼을 LOW로
 * 구동하지 않으므로 ROW->COL 관통전류(쇼트) 우려가 없다.
 */
#include "keys.h"

#ifdef _USE_HW_KEYS

#include "bsp.h"


static const uint8_t row_pins[MATRIX_ROWS] = KEY_ROW_PINS;
static const uint8_t col_pins[MATRIX_COLS] = KEY_COL_PINS;

static uint32_t row_mask = 0;
static uint32_t col_mask = 0;
static uint16_t matrix[MATRIX_ROWS];
static bool     is_init = false;


static inline void keySettle(void)
{
  /* 샘플링 전 행/열 RC 안정용 대략적 busy-delay.
   * TODO(확인): 컬럼 안정 시간이 부족한 보드면 반복 횟수 조정 필요. */
  for (volatile uint32_t i = 0; i < 2000; i++)
    __NOP();
}

bool keysInit(void)
{
  row_mask = 0;
  col_mask = 0;
  for (int r = 0; r < MATRIX_ROWS; r++)
    row_mask |= (1u << row_pins[r]);
  for (int c = 0; c < MATRIX_COLS; c++)
    col_mask |= (1u << col_pins[c]);

  GPIO_SetMode(PA, row_mask, GPIO_MODE_OUTPUT);
  PA->DOUT &= ~row_mask;

  GPIO_SetMode(PB, col_mask, GPIO_MODE_INPUT);
  GPIO_SetPullCtl(PB, col_mask, GPIO_PUSEL_PULL_DOWN);

  for (int r = 0; r < MATRIX_ROWS; r++)
    matrix[r] = 0;

  is_init = true;
  return true;
}

bool keysIsBusy(void)
{
  return false;
}

bool keysUpdate(void)
{
  if (!is_init)
    keysInit();

  for (int r = 0; r < MATRIX_ROWS; r++)
  {
    uint32_t pin;
    uint16_t cols = 0;

    /* 이 행만 HIGH 구동 (나머지 행 비트는 LOW 유지). */
    PA->DOUT = (PA->DOUT & ~row_mask) | (1u << row_pins[r]);
    keySettle();

    /* PB 핀 원본 워드를 논리 컬럼 인덱스로 역매핑. */
    pin = PB->PIN;
    for (int c = 0; c < MATRIX_COLS; c++)
    {
      if (pin & (1u << col_pins[c]))
        cols |= (1u << c);
    }
    matrix[r] = cols;
  }

  PA->DOUT &= ~row_mask;   /* idle: 모든 행 LOW */
  return true;
}

bool keysGetPressed(uint16_t row, uint16_t col)
{
  if (row >= MATRIX_ROWS || col >= MATRIX_COLS)
    return false;

  return (matrix[row] & (1u << col)) ? true : false;
}

#endif
