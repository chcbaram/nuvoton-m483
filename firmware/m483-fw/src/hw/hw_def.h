#ifndef HW_DEF_H_
#define HW_DEF_H_



#include "bsp.h"


#define _DEF_FIRMWATRE_VERSION    "V260718R1"
#define _DEF_BOARD_NAME           "WISH-QMK-8K"


/* Retained boot-flag block shared with m483-boot (BOOT_APP builds). */
#define BOOT_FLAG_ADDR            0x20027FE0UL
#define BOOT_REQUEST_MAGIC        0x424F4F54UL              /* "BOOT" */



#define _USE_HW_LED
#define      HW_LED_MAX_CH          1

#define _USE_HW_UART
#define      HW_UART_MAX_CH         1
#define      HW_UART_CH_SWD         _DEF_UART1
#define      HW_UART_CH_CLI         _DEF_UART1

#define _USE_HW_LOG
#define      HW_LOG_CH              _DEF_UART1
#define      HW_LOG_BOOT_BUF_MAX    2048
#define      HW_LOG_LIST_BUF_MAX    4096

#define _USE_HW_CLI
#define      HW_CLI_CMD_LIST_MAX    32
#define      HW_CLI_CMD_NAME_MAX    16
#define      HW_CLI_LINE_HIS_MAX    8
#define      HW_CLI_LINE_BUF_MAX    64

#define _USE_HW_BUTTON
#define      HW_BUTTON_MAX_CH       1

#define _USE_HW_RESET

#define _USE_HW_USB
#define      HW_USB_HID             1

#define _USE_HW_I2C
#define      HW_I2C_MAX_CH          1

#define _USE_HW_EEPROM
#define      EEPROM_CHIP_ZD24C128

#define _USE_HW_WS2812
#define      HW_WS2812_MAX_CH       45     /* 실제 보드 LED 개수에 맞게 조정 */

#define _USE_HW_KEYS
/* 정적 할당 상한. 실제 ROW/COL 개수·핀순서는 보드 config 가 keysInit(cfg) 로 주입한다.
 * ROW=PA / COL=PB 단일 16비트 포트이므로 각 <= 16.                                   */
#define      HW_KEYS_ROW_MAX        8
#define      HW_KEYS_COL_MAX        16
/* 6KRO boot 리포트의 keycode 슬롯 수 (report.h: KEYBOARD_REPORT_SIZE = +2). */
#define      HW_KEYS_PRESS_MAX      6


//-- USE CLI
//
#define _USE_CLI_HW_LED             1
#define _USE_CLI_HW_RESET           1
#define _USE_CLI_HW_I2C             1
#define _USE_CLI_HW_EEPROM          1
#define _USE_CLI_HW_WS2812          1
#define _USE_CLI_HW_KEYS            1


#endif
