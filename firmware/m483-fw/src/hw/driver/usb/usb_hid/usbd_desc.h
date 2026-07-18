/*
 * usbd_desc.h
 *
 *  HSUSBD HID keyboard descriptors (single boot-keyboard interface, High-Speed).
 *  baram-qmk-8k 의 usb_hid/usbd_desc.h 위치/역할을 미러링.
 */

#ifndef SRC_HW_DRIVER_USB_USB_HID_USBD_DESC_H_
#define SRC_HW_DRIVER_USB_USB_HID_USBD_DESC_H_

#ifdef __cplusplus
extern "C" {
#endif

#include "hw_def.h"

#ifdef _USE_HW_USB

#include "NuMicro.h"

/* Vendor / Product ID */
#define USBD_VID                0x0416
#define USBD_PID                0xC145

/* HID Class-Specific Requests */
#define HID_GET_REPORT          0x01
#define HID_GET_IDLE            0x02
#define HID_GET_PROTOCOL        0x03
#define HID_SET_REPORT          0x09
#define HID_SET_IDLE            0x0A
#define HID_SET_PROTOCOL        0x0B

/* HID Interface protocol */
#define HID_NONE                0x00
#define HID_KEYBOARD            0x01
#define HID_MOUSE               0x02

/* HID Protocol type */
#define HID_BOOT_PROTOCOL       0x00
#define HID_REPORT_PROTOCOL     0x01

/* Keyboard LED output report bits */
#define HID_LED_NumLock         0x01
#define HID_LED_CapsLock        0x02
#define HID_LED_ScrollLock      0x04
#define HID_LED_Compose         0x08
#define HID_LED_Kana            0x10
#define HID_LED_ALL             0xFF

/*-------------------------------------------------------------*/
/* Endpoint packet sizes / FIFO buffer layout                  */
/* CEP + single interrupt-IN keyboard endpoint (EPA).          */
/*-------------------------------------------------------------*/
#define CEP_MAX_PKT_SIZE        64
#define CEP_OTHER_MAX_PKT_SIZE  64
#define EPA_MAX_PKT_SIZE        64
#define EPA_OTHER_MAX_PKT_SIZE  64

#define CEP_BUF_BASE            0
#define CEP_BUF_LEN             CEP_MAX_PKT_SIZE
#define EPA_BUF_BASE            (CEP_BUF_BASE + CEP_BUF_LEN)
#define EPA_BUF_LEN             EPA_MAX_PKT_SIZE

/* Keyboard interrupt-IN endpoint number (bEndpointAddress = 0x81) */
#define INT_IN_EP_NUM_KB        0x01

/*
 * bInterval for the interrupt-IN endpoint.
 * High-Speed: interval = 2^(bInterval-1) microframes (125us each).
 * bInterval = 1  => 1 microframe = 125us => 8000 Hz (8K).
 */
#define HID_KBD_INT_IN_INTERVAL 0x01

#define USBD_SELF_POWERED       0
#define USBD_REMOTE_WAKEUP      0
#define USBD_MAX_POWER          50   /* x2mA => 100mA */

/* config + (interface + hid + endpoint) for a single keyboard interface */
#define LEN_CONFIG_AND_SUBORDINATE  (LEN_CONFIG + LEN_INTERFACE + LEN_HID + LEN_ENDPOINT)

/* HID keyboard boot report is 8 bytes: modifier + reserved + keycode[6] */
#define HID_KBD_REPORT_SIZE     8

extern S_HSUSBD_INFO_T gsHSInfo;
extern uint8_t HID_KeyboardReportDescriptor[];

#endif /* _USE_HW_USB */

#ifdef __cplusplus
}
#endif

#endif /* SRC_HW_DRIVER_USB_USB_HID_USBD_DESC_H_ */
