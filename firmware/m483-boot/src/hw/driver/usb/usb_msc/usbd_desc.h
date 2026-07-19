/*
 * usbd_desc.h
 *
 *  USB descriptor table for the full-speed MSC (BOT / SCSI) bootloader device.
 */
#ifndef SRC_HW_DRIVER_USB_USB_MSC_USBD_DESC_H_
#define SRC_HW_DRIVER_USB_USB_MSC_USBD_DESC_H_

#ifdef __cplusplus
extern "C" {
#endif

#include "hw_def.h"

/* gsHSInfo is declared extern by the HSUSBD driver (hsusbd.h); it is defined
 * in descriptors.c and passed to HSUSBD_Open(). The individual descriptor
 * arrays are exposed here for completeness. */
extern uint8_t gu8DeviceDescriptor[];
extern uint8_t gu8ConfigDescriptor[];
extern uint8_t gu8QualifierDescriptor[];
extern uint8_t gu8ConfigDescriptorFS[];
extern uint8_t gu8OtherConfigDescriptorHS[];
extern uint8_t gu8OtherConfigDescriptorFS[];
extern uint8_t *gpu8UsbString[4];

#ifdef __cplusplus
}
#endif

#endif /* SRC_HW_DRIVER_USB_USB_MSC_USBD_DESC_H_ */
