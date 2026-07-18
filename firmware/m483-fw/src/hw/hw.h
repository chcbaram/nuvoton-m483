#ifndef HW_H_
#define HW_H_

#ifdef __cplusplus
extern "C" {
#endif

#include "hw_def.h"


#include "led.h"
#include "uart.h"
#include "log.h"
#include "cli.h"
#include "button.h"
#include "reset.h"
#include "usb.h"
#include "i2c.h"
#include "eeprom.h"
#include "ws2812.h"
#include "keys.h"

bool hwInit(void);


#ifdef __cplusplus
}
#endif

#endif