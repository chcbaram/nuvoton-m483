#ifndef HW_DEF_H_
#define HW_DEF_H_



#include "bsp.h"


#define _DEF_FIRMWATRE_VERSION    "V240516R1"
#define _DEF_BOARD_NAME           "M483-FW"



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


//-- USE CLI
//
#define _USE_CLI_HW_LED             1
#define _USE_CLI_HW_RESET           1
#define _USE_CLI_HW_I2C             1
#define _USE_CLI_HW_EEPROM          1
#define _USE_CLI_HW_WS2812          1


#endif
