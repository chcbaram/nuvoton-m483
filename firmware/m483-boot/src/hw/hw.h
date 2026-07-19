#ifndef HW_H_
#define HW_H_

#ifdef __cplusplus
extern "C" {
#endif

#include "hw_def.h"


#include "led.h"
#include "flash.h"
#include "reset.h"
#include "keys.h"
#include "uart.h"
#include "log.h"
#include "usb.h"


bool hwInit(void);


#ifdef __cplusplus
}
#endif

#endif