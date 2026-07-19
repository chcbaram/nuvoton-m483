/*
 * usb.c
 *
 *  Umbrella layer tying the HSUSBD backend (usbd_conf) to the MSC class
 *  (usb_msc/usbd_msc). Keeps ap.c decoupled from the HSUSBD/MSC internals.
 *
 *  WRITE10 wiring:
 *    - usbUpdate() runs the polled BOT/SCSI worker in the main loop. On WRITE10
 *      it receives the whole data phase inline and passes each 512-byte sector
 *      to the callback registered via mscSetWriteCb().
 *    - ap.c registers a callback that calls uf2_write_block(); the USB layer
 *      stays decoupled from uf2.
 */
#include "usb.h"

#include "usbd_conf.h"
#include "usb_msc/usbd_msc.h"

void usbInit(void)
{
  /* usbdConfInit() performs PHY/clock bring-up, HSUSBD_Open(mscClassRequest),
   * mscInit() (MSC endpoint config), IRQ enable and the full-speed attach. */
  usbdConfInit();
}

void usbUpdate(void)
{
  mscProcess();
}

bool usbIsConfigured(void)
{
  return usbdConfIsConfigured();
}

/* mscSetWriteCb() is provided by usb_msc/usbd_msc.c and re-exported through
 * usb.h for ap.c; no wrapper needed. */
