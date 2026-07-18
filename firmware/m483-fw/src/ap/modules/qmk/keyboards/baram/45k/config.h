#pragma once


#define KBD_NAME                    "WISH45-8K"

/* VIA JSON(WISH45-8K.json)의 vendorId/productId 와 반드시 일치.
 * PID 는 기존 보드들과 겹치지 않게 배정 (baram 0x5201~0x5207,0x5210 / wish 0x5208~0x520A). */
#define USB_VID                     0x0483
#define USB_PID                     0x520B

/* EEPROM: 실제 I2C ZD24C128(16KB) 사용 (하드웨어는 hw_def.h 가 선택) */
#define EEPROM_CHIP_ZD24C128
#define EECONFIG_USER_DATA_SIZE     128
#define TOTAL_EEPROM_BYTE_COUNT     4096

#define DYNAMIC_KEYMAP_LAYER_COUNT  8

#define MATRIX_ROWS                 4
#define MATRIX_COLS                 12

/*
 * M483 매트릭스 핀맵 (포트 고정: ROW=PA, COL=PB. 포트 내 비트 인덱스만 매트릭스 순서로).
 * port/matrix.c 가 keys_cfg_t 로 keysInit() 에 주입한다. 보드가 달라지면 여기만 바꾼다.
 *   ROW0~3 = PA.0~3
 *   COL0~8 = PB.15~PB.7,  COL9~11 = PB.2/PB.1/PB.0
 */
#define KEY_ROW_PINS                { 0, 1, 2, 3 }
#define KEY_COL_PINS                { 15, 14, 13, 12, 11, 10, 9, 8, 7, 2, 1, 0 }

/* eager 디바운스: 눌림 에지는 즉시 등록(지연 무영향), 이후 DEBOUNCE ms 재감지 잠금.
 * (Phase B 에서 VIA 런타임 조절 예정) */
#define DEBOUNCE                    20
