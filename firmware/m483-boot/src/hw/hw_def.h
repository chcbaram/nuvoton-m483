#ifndef HW_DEF_H_
#define HW_DEF_H_



#include "bsp.h"


#define _DEF_FIRMWATRE_VERSION    "B250719R1"
#define _DEF_BOARD_NAME           "WISH-BOOT"


#define _USE_HW_FLASH
#define _USE_HW_RESET
#define _USE_HW_KEYS

#define _USE_HW_UART
#define      HW_UART_MAX_CH         1
#define      HW_UART_CH_LOG         _DEF_UART1

#define _USE_HW_LOG
#define      HW_LOG_CH              _DEF_UART1
#define      HW_LOG_BOOT_BUF_MAX    1024
#define      HW_LOG_LIST_BUF_MAX    1024

#define _USE_HW_LED
#define      HW_LED_MAX_CH          1


/* APROM map : boot 0..0xBFFF (48K) | app 0xC000..0x7EFFF | tag 0x7F000 (last page) */
#define FLASH_ADDR_BOOT           0x00000000UL
#define FLASH_SIZE_BOOT           0x0000C000UL
#define FLASH_ADDR_FIRM           0x0000C000UL
/* 앱 버전 구조체(firm_ver_t) 위치. m483-fw gcc_arm_boot.ld의 VER 영역(0xC400)과
 * 반드시 일치해야 한다: VECTOR 0xC000(1K) | VER 0xC400(1K) | FLASH 0xC800. */
#define FLASH_ADDR_VER            0x0000C400UL
#define FLASH_APROM_END           0x00080000UL
#define FLASH_SIZE_FIRM           (FLASH_APROM_END - FLASH_ADDR_FIRM)
#define FLASH_PAGE_SIZE           0x1000UL

#define FLASH_ADDR_TAG            0x0007F000UL
#define FLASH_APP_LIMIT           FLASH_ADDR_TAG

/* Retained boot-flag block: top 32B of SRAM, reserved in gcc_arm.ld (RAM-32). */
#define BOOT_FLAG_ADDR            0x20027FE0UL
#define BOOT_REQUEST_MAGIC        0x424F4F54UL

#define BOARD_UF2_FAMILY_ID       0xFFFF0002UL
#define BOARD_FLASH_APP_START     FLASH_ADDR_FIRM

#define USB_VID                   0x0483
#define USB_PID                   0x520C


/* Key matrix : ROW = PA (push-pull out), COL = PB (input + pull-down). GPIO only.
 * Shared across boards - override the pinmap per board here. */
#define MATRIX_ROWS               4
#define MATRIX_COLS               12
#define KEY_ROW_PINS              { 0, 1, 2, 3 }
#define KEY_COL_PINS              { 15, 14, 13, 12, 11, 10, 9, 8, 7, 2, 1, 0 }

/* Bootloader-entry key held at reset (default ESC = row0,col0). */
#define BOOT_KEY_ROW              0
#define BOOT_KEY_COL              0
#define BOOT_KEY_SETTLE_MS        5


#endif
