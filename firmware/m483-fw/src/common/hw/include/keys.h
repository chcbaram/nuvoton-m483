#ifndef KEYS_H_
#define KEYS_H_

#ifdef __cplusplus
extern "C" {
#endif

#include "hw_def.h"

#ifdef _USE_HW_KEYS

/* 정적 할당 상한 (ROW=PA / COL=PB 단일 16비트 포트) */
#define KEY_ROW_MAX   HW_KEYS_ROW_MAX
#define KEY_COL_MAX   HW_KEYS_COL_MAX

/*
 * 보드별 매트릭스 설정. 포트는 고정(ROW=PA, COL=PB)이고 개수·핀 비트순서만 다르다.
 * row_bits[]/col_bits[] 는 각 포트 내 비트 인덱스를 매트릭스 순서로 나열한다.
 * ap 레이어(port/matrix.c)가 보드 config 로 채워 keysInit() 에 주입한다.
 */
typedef struct
{
  const uint8_t *row_bits;   /* PA 비트 인덱스, 길이 row_cnt */
  uint8_t        row_cnt;
  const uint8_t *col_bits;   /* PB 비트 인덱스, 길이 col_cnt */
  uint8_t        col_cnt;
} keys_cfg_t;


bool keysInit(const keys_cfg_t *cfg);
bool keysIsBusy(void);
bool keysUpdate(void);
bool keysGetPressed(uint16_t row, uint16_t col);

/* 행 비트마스크(col c -> bit c)를 그대로 반환. QMK matrix_row_t 와 비트순서 동일 -> 핫패스용. */
uint16_t keysGetRow(uint16_t row);

#endif

#ifdef __cplusplus
}
#endif

#endif
