/*
 * port/matrix.c
 *
 *  QMK matrix <-> M483 고속 스캐너(keys.c) 브리지.
 *   - matrix_init() : 보드 config 의 KEY_ROW_PINS/KEY_COL_PINS 로 keysInit(cfg) 주입.
 *   - matrix_scan() : keysUpdate()+keysGetPressed() 로 raw_matrix 채우고 debounce().
 *   - 저지연: 새로 눌린 키의 접점 시각을 usbHidSetPressTime() 으로 전달(레이턴시 계측).
 *  (baram 의 chattering/DWT/web 텔레메트리 훅은 Phase B 로 분리)
 */

#include "matrix.h"
#include "debounce.h"
#include "keyboard.h"
#include "util.h"
#include <stdbool.h>
#include <stdint.h>
#include <string.h>
#include "cli.h"
#include "keys.h"
#include "bsp.h"
#include "usbd_hid.h"
#include "prof.h"
#ifdef WEB_HID_CHECK
#include "chattering.h"
#endif


#ifndef KEY_ROW_PINS
#error "board config.h must define KEY_ROW_PINS { ... } (PA bit indices)"
#endif
#ifndef KEY_COL_PINS
#error "board config.h must define KEY_COL_PINS { ... } (PB bit indices)"
#endif

static const uint8_t row_pins[MATRIX_ROWS] = KEY_ROW_PINS;
static const uint8_t col_pins[MATRIX_COLS] = KEY_COL_PINS;

static matrix_row_t raw_matrix[MATRIX_ROWS];
static matrix_row_t matrix[MATRIX_ROWS];
static uint32_t     key_scan_time = 0;

/* 레이턴시 프로파일링 : matrix_scan 전체 소요시간 + 눌림 확정 스캔 마커 */
volatile uint32_t mscan_us   = 0;
volatile uint8_t  press_scan_flag = 0;

static void cliMatrix(cli_args_t *args);


void matrix_init(void)
{
  keys_cfg_t cfg;

  memset(matrix, 0, sizeof(matrix));
  memset(raw_matrix, 0, sizeof(raw_matrix));

  cfg.row_bits = row_pins;
  cfg.row_cnt  = MATRIX_ROWS;
  cfg.col_bits = col_pins;
  cfg.col_cnt  = MATRIX_COLS;
  keysInit(&cfg);

  debounce_init(MATRIX_ROWS);

  cliAdd("matrix", cliMatrix);
}

void matrix_print(void)
{
}

bool matrix_can_read(void)
{
  return true;
}

matrix_row_t matrix_get_row(uint8_t row)
{
  return matrix[row];
}

uint8_t matrix_scan(void)
{
  matrix_row_t curr_matrix[MATRIX_ROWS] = {0};
  uint32_t     pre_time;
  bool         changed;
  uint32_t     prof_c0 = profNow();

  /* 키별 raw 접점 시각 (디바운스 확정 시 최대 지연 T0 로 사용) */
  static uint32_t     key_raw_time[MATRIX_ROWS][MATRIX_COLS] = {{0}};
  static matrix_row_t prev_cooked[MATRIX_ROWS] = {0};

  pre_time = micros();

  keysUpdate();                                  /* col_raw -> 행 비트마스크(1회 remap) */
  for (uint8_t r = 0; r < MATRIX_ROWS; r++)
    curr_matrix[r] = keysGetRow(r);              /* 비트순서 동일 -> 그대로 복사(48-call 제거) */

  key_scan_time = micros() - pre_time;

  /* raw 변화한 키별 접점 시각 기록 (raw_matrix 갱신 전에).
   * 변한 비트만 순회(CTZ) -> 변화 없는 행/스캔은 0회. (전 48키 순회 제거)
   * 동시에 any_change 로 변화 여부를 누적 -> 아래 memcmp 제거. */
  matrix_row_t any_change = 0;
  for (uint8_t r = 0; r < MATRIX_ROWS; r++)
  {
    matrix_row_t delta = (matrix_row_t)(curr_matrix[r] ^ raw_matrix[r]);
    any_change |= delta;
    while (delta)
    {
      uint8_t c = (uint8_t)__builtin_ctz(delta);
      key_raw_time[r][c] = pre_time;
      delta &= (matrix_row_t)(delta - 1);   /* 최하위 set 비트 제거 */
    }
  }

#ifdef WEB_HID_CHECK
  /* 채터링 점검(웹에서 활성화 시에만) : raw 전이를 raw_matrix 갱신 전에 샘플링 */
  if (chattering_is_enabled())
    chattering_raw_scan(curr_matrix, raw_matrix, pre_time);
#endif

  changed = (any_change != 0);   /* memcmp 제거 : delta 루프에서 이미 계산 */
  if (changed)
    memcpy(raw_matrix, curr_matrix, sizeof(curr_matrix));

  changed = debounce(raw_matrix, matrix, MATRIX_ROWS, changed);
  if (changed)
  {
    /* 새로 눌린(0->1) 키들 중 가장 이른 접점 시각 = 최대 지연 -> T0 */
    uint32_t t0    = pre_time;
    bool     found = false;

    for (uint8_t r = 0; r < MATRIX_ROWS; r++)
    {
      matrix_row_t press = matrix[r] & (matrix_row_t)~prev_cooked[r];
      while (press)
      {
        uint8_t   c  = (uint8_t)__builtin_ctz(press);
        uint32_t  kt = key_raw_time[r][c];
        if (!found || (int32_t)(t0 - kt) > 0) { t0 = kt; found = true; }
        press &= (matrix_row_t)(press - 1);
      }
      prev_cooked[r] = matrix[r];
    }

    if (found)
    {
      usbHidSetPressTime(t0);   /* 접점 시각 -> 레이턴시 기준(디바운스 포함 총지연) */
      press_scan_flag = 1;         /* 이 스캔에서 눌림 확정 -> 프로파일 마커 */
    }
  }

  mscan_us = micros() - pre_time;   /* matrix_scan 전체 소요시간 */
  if (press_scan_flag)
    profAdd(PROF_MATRIX, "matrix", profNow() - prof_c0);
  return (uint8_t)changed;
}


static void cliMatrix(cli_args_t *args)
{
  bool ret = false;

  if (args->argc == 1 && args->isStr(0, "info"))
  {
    usb_hid_rate_info_t hid_info;

    usbHidGetRateInfo(&hid_info);
    cliPrintf("MATRIX     : %d x %d\n", MATRIX_ROWS, MATRIX_COLS);
    cliPrintf("Poll Rate  : %d Hz (min %d us, max %d us)\n",
              (int)hid_info.freq_hz, (int)hid_info.time_min, (int)hid_info.time_max);
    cliPrintf("Scan Time  : %d us\n", (int)key_scan_time);
    ret = true;
  }

  if (ret == false)
  {
    cliPrintf("matrix info\n");
  }
}
