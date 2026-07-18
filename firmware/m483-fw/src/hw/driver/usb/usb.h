/*
 * usb.h
 *
 *  USB 상위 레이어 (모드 선택 / 공개 진입점).
 *  baram-qmk-8k 의 usb/usb.h 를 미러링 (현재는 HID 모드만 구현).
 */

#ifndef SRC_HW_DRIVER_USB_USB_H_
#define SRC_HW_DRIVER_USB_USB_H_

#ifdef __cplusplus
extern "C" {
#endif

#include "hw_def.h"

#ifdef _USE_HW_USB

#include "usbd_conf.h"

#if HW_USB_HID == 1
#include "usb_hid/usbd_hid.h"
#endif


typedef enum UsbMode
{
  USB_NON_MODE,
  USB_CDC_MODE,
  USB_MSC_MODE,
  USB_HID_MODE,
  USB_CMP_MODE,
} UsbMode_t;


bool      usbInit(void);
bool      usbBegin(UsbMode_t usb_mode);
bool      usbIsOpen(void);
bool      usbIsConnect(void);
UsbMode_t usbGetMode(void);

#endif /* _USE_HW_USB */

#ifdef __cplusplus
}
#endif

#endif /* SRC_HW_DRIVER_USB_USB_H_ */
