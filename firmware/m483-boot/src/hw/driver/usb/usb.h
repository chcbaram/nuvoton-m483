/*
 * usb.h
 *
 *  Umbrella entry points for the full-speed USB MSC (UF2) bootloader stack.
 *  ap.c uses only these:
 *
 *    usbInit()          - PHY/clock, HSUSBD_Open, MSC EP config, FS attach.
 *    usbUpdate()        - polled BOT/SCSI worker; call from the main loop.
 *    usbIsConfigured()  - true once the host SET_CONFIGURATION completes.
 *    mscSetWriteCb(cb)  - register the WRITE10 sink. Each 512-byte sector the
 *                         host writes is passed to cb(lba, data) inline (main
 *                         loop). Typical use:
 *                             static int onWrite(uint32_t lba, uint8_t *d) {
 *                               return uf2_write_block(d);
 *                             }
 *                             mscSetWriteCb(onWrite);
 */
#ifndef SRC_HW_DRIVER_USB_USB_H_
#define SRC_HW_DRIVER_USB_USB_H_

#ifdef __cplusplus
extern "C" {
#endif

#include "hw_def.h"

/* WRITE10 sink callback: cb(lba, data) for each 512-byte sector. */
typedef int (*msc_write_cb_t)(uint32_t lba, uint8_t *data);

void usbInit(void);
void usbUpdate(void);
bool usbIsConfigured(void);
void mscSetWriteCb(msc_write_cb_t cb);

#ifdef __cplusplus
}
#endif

#endif /* SRC_HW_DRIVER_USB_USB_H_ */
