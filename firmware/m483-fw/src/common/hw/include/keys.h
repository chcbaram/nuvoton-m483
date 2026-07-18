#ifndef KEYS_H_
#define KEYS_H_

#ifdef __cplusplus
extern "C" {
#endif

#include "hw_def.h"

#ifdef _USE_HW_KEYS

#define MATRIX_ROWS   HW_KEYS_MATRIX_ROWS
#define MATRIX_COLS   HW_KEYS_MATRIX_COLS


bool keysInit(void);
bool keysIsBusy(void);
bool keysUpdate(void);
bool keysGetPressed(uint16_t row, uint16_t col);

#endif

#ifdef __cplusplus
}
#endif

#endif
