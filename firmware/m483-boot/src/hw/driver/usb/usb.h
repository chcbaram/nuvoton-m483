/*
 * usb.h
 *
 *  Umbrella entry points for the full-speed USB MSC (UF2) bootloader stack.
 *  Only the bus-level lifecycle lives here:
 *
 *    usbInit()          - PHY/clock, HSUSBD_Open, MSC EP config, FS attach.
 *    usbUpdate()        - polled BOT/SCSI worker; call from the main loop.
 *    usbIsConfigured()  - true once the host SET_CONFIGURATION completes.
 *
 *  MSC-class API (write sink, eject latch, ...) lives in usb_msc/usbd_msc.h;
 *  include that directly where the MSC layer is used (e.g. ap.c).
 */
#ifndef SRC_HW_DRIVER_USB_USB_H_
#define SRC_HW_DRIVER_USB_USB_H_

#ifdef __cplusplus
extern "C" {
#endif

#include "hw_def.h"

void usbInit(void);
void usbUpdate(void);
bool usbIsConfigured(void);

#ifdef __cplusplus
}
#endif

#endif /* SRC_HW_DRIVER_USB_USB_H_ */
