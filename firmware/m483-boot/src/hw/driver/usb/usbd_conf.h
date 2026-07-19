/*
 * usbd_conf.h
 *
 *  HSUSBD low-level backend for the full-speed MSC bootloader device.
 *  - HSUSB PHY / clock bring-up (ported from m483-fw usbd_conf.c)
 *  - HSUSBD_Open + MSC endpoint config + IRQ enable
 *  - FULL-SPEED attach (HISPDEN left clear, DP pull-up via CLR_SE0)
 *  - USBD20_IRQHandler
 */
#ifndef SRC_HW_DRIVER_USB_USBD_CONF_H_
#define SRC_HW_DRIVER_USB_USBD_CONF_H_

#ifdef __cplusplus
extern "C" {
#endif

#include "hw_def.h"

/* PHY/clock + HSUSBD_Open + MSC EP config + IRQ enable + FS attach. */
void usbdConfInit(void);

/* SET_CONFIGURATION completed ? */
bool usbdConfIsConfigured(void);

/* Force a clean bus disconnect: mask the HSUSBD IRQ and drive SE0 (drop the D+
 * pull-up) so the host registers a device removal. Used on eject before reset. */
void usbdConfDisconnect(void);

#ifdef __cplusplus
}
#endif

#endif /* SRC_HW_DRIVER_USB_USBD_CONF_H_ */
