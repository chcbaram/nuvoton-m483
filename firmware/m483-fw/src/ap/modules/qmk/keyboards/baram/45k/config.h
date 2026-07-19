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

/* NKRO 는 VIA FEATURE>QMK 토글(id_qmk_nkro) 로 제어 - keymap_config.nkro 에 영구 저장.
 * (FORCE_NKRO 를 쓰면 부팅마다 강제 ON 되어 토글 OFF 가 안 남으므로 사용 안 함) */

/* Grave Escape : QK_GESC 키코드(평소 ESC, Shift/GUI 시 grave). baram/VENOM 과 동일. */
#define GRAVE_ESC_ENABLE


/*----------------------------------------------------------------------------*/
/* RGB Matrix (per-key SK6812/WS2812, 회로도상 45개 체인 = HW_WS2812_MAX_CH)    */
/*----------------------------------------------------------------------------*/
#define RGB_MATRIX_LED_COUNT          45     /* == hw_def.h HW_WS2812_MAX_CH */
#define RGB_MATRIX_MAXIMUM_BRIGHTNESS 128    /* 전력 예산 (VIA 밝기 0~255 를 이 안으로 스케일) */
#define RGB_MATRIX_DEFAULT_VAL        80
#define RGB_MATRIX_DEFAULT_ON         true
#define RGB_MATRIX_DEFAULT_MODE       RGB_MATRIX_CYCLE_LEFT_RIGHT
#define RGB_MATRIX_SLEEP
/* LED_PROCESS_LIMIT 은 정의하지 않음 -> QMK 기본 청킹((45+4)/5=9) 으로 per-call CPU 최소화(8K) */

/* 이펙트 (qmk-zephyr wish40 세트) — VIA Lighting 드롭다운 순서와 일치해야 함 */
#define ENABLE_RGB_MATRIX_GRADIENT_UP_DOWN
#define ENABLE_RGB_MATRIX_GRADIENT_LEFT_RIGHT
#define ENABLE_RGB_MATRIX_BREATHING
#define ENABLE_RGB_MATRIX_BAND_VAL
#define ENABLE_RGB_MATRIX_BAND_PINWHEEL_VAL
#define ENABLE_RGB_MATRIX_BAND_SPIRAL_VAL
#define ENABLE_RGB_MATRIX_CYCLE_ALL
#define ENABLE_RGB_MATRIX_CYCLE_LEFT_RIGHT
#define ENABLE_RGB_MATRIX_CYCLE_UP_DOWN
#define ENABLE_RGB_MATRIX_RAINBOW_MOVING_CHEVRON
#define ENABLE_RGB_MATRIX_CYCLE_OUT_IN
#define ENABLE_RGB_MATRIX_CYCLE_OUT_IN_DUAL
#define ENABLE_RGB_MATRIX_CYCLE_PINWHEEL
#define ENABLE_RGB_MATRIX_CYCLE_SPIRAL
#define ENABLE_RGB_MATRIX_RAINBOW_BEACON
#define ENABLE_RGB_MATRIX_RAINBOW_PINWHEELS
#define ENABLE_RGB_MATRIX_PIXEL_FLOW
