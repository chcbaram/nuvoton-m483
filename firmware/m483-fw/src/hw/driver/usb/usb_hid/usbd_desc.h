/*
 * usbd_desc.h
 *
 *  HSUSBD HID 복합장치 디스크립터 (QMK 네이티브 3-인터페이스, High-Speed).
 *   IF0 Keyboard(boot 6KRO) : EPA IN  8B   (bInterval=1 => 8K)
 *   IF1 Raw/VIA             : EPB IN 32B / EPC OUT 32B (usage page 0xFF60)
 *   IF2 Shared              : EPD IN 32B   (report-id 멀티플렉스: NKRO=6/System=3/Consumer=4)
 *
 *  NKRO 는 QMK 순정과 동일하게 키보드 EP 가 아니라 shared EP(EPD)를 탄다.
 */

#ifndef SRC_HW_DRIVER_USB_USB_HID_USBD_DESC_H_
#define SRC_HW_DRIVER_USB_USB_HID_USBD_DESC_H_

#ifdef __cplusplus
extern "C" {
#endif

#include "hw_def.h"

#ifdef _USE_HW_USB

#include "NuMicro.h"

/* 보드 config 의 USB_VID/USB_PID 를 사용(있으면). VIA JSON 과 일치해야 한다. */
#ifdef QMK_KEYMAP_CONFIG_H
#include QMK_KEYMAP_CONFIG_H
#endif
#ifndef USB_VID
#define USB_VID                 0x0483
#endif
#ifndef USB_PID
#define USB_PID                 0x5210
#endif

#define USBD_VID                USB_VID
#define USBD_PID                USB_PID

/* HID Class-Specific Requests */
#define HID_GET_REPORT          0x01
#define HID_GET_IDLE            0x02
#define HID_GET_PROTOCOL        0x03
#define HID_SET_REPORT          0x09
#define HID_SET_IDLE            0x0A
#define HID_SET_PROTOCOL        0x0B

/* HID Interface protocol / subclass */
#define HID_NONE                0x00
#define HID_KEYBOARD            0x01
#define HID_MOUSE               0x02

/* HID Protocol type (SET_PROTOCOL wValue) */
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
/* Endpoint 번호(bEndpointAddress 하위 니블) / 인터페이스 번호   */
/*-------------------------------------------------------------*/
#define IF_NUM_KBD              0x00
#define IF_NUM_VIA             0x01
#define IF_NUM_SHARED           0x02
#define NUM_INTERFACES          3

#define INT_IN_EP_NUM_KB        0x01   /* EPA IN  : keyboard */
#define INT_IN_EP_NUM_VIA       0x02   /* EPB IN  : VIA response */
#define OUT_EP_NUM_VIA          0x03   /* EPC OUT : VIA command */
#define INT_IN_EP_NUM_SHARED    0x04   /* EPD IN  : NKRO/system/consumer */

/*-------------------------------------------------------------*/
/* Packet sizes / FIFO 버퍼 배치                                */
/*-------------------------------------------------------------*/
#define CEP_MAX_PKT_SIZE        64
#define CEP_OTHER_MAX_PKT_SIZE  64
#define EPA_MAX_PKT_SIZE        8     /* keyboard boot report */
#define EPA_OTHER_MAX_PKT_SIZE  8
#define EPB_MAX_PKT_SIZE        32    /* VIA IN  */
#define EPB_OTHER_MAX_PKT_SIZE  32
#define EPC_MAX_PKT_SIZE        32    /* VIA OUT */
#define EPC_OTHER_MAX_PKT_SIZE  32
#define EPD_MAX_PKT_SIZE        32    /* shared IN */
#define EPD_OTHER_MAX_PKT_SIZE  32

#define CEP_BUF_BASE            0
#define CEP_BUF_LEN             CEP_MAX_PKT_SIZE
#define EPA_BUF_BASE            (CEP_BUF_BASE + CEP_BUF_LEN)
#define EPA_BUF_LEN             64
#define EPB_BUF_BASE            (EPA_BUF_BASE + EPA_BUF_LEN)
#define EPB_BUF_LEN             64
#define EPC_BUF_BASE            (EPB_BUF_BASE + EPB_BUF_LEN)
#define EPC_BUF_LEN             64
#define EPD_BUF_BASE            (EPC_BUF_BASE + EPC_BUF_LEN)
#define EPD_BUF_LEN             64

/*
 * bInterval (High-Speed: 2^(bInterval-1) microframes @125us).
 * bInterval=1 => 125us => 8000 Hz. 키보드/NKRO 는 8K, VIA 는 latency 무관하나 통일.
 */
#define HID_KBD_INT_IN_INTERVAL    0x01
#define HID_VIA_INT_INTERVAL       0x01
#define HID_SHARED_INT_IN_INTERVAL 0x01

#define USBD_SELF_POWERED       0
#define USBD_REMOTE_WAKEUP      0
#define USBD_MAX_POWER          50   /* x2mA => 100mA */

/* config subordinate 길이 : IF0(1EP) + IF1(2EP) + IF2(1EP) */
#define LEN_IF_KBD              (LEN_INTERFACE + LEN_HID + LEN_ENDPOINT)
#define LEN_IF_VIA             (LEN_INTERFACE + LEN_HID + LEN_ENDPOINT + LEN_ENDPOINT)
#define LEN_IF_SHARED           (LEN_INTERFACE + LEN_HID + LEN_ENDPOINT)
#define LEN_CONFIG_AND_SUBORDINATE (LEN_CONFIG + LEN_IF_KBD + LEN_IF_VIA + LEN_IF_SHARED)

/* config descriptor 내 각 인터페이스 HID 디스크립터의 바이트 오프셋 */
#define CFG_HID_IDX_KBD         (LEN_CONFIG + LEN_INTERFACE)
#define CFG_HID_IDX_VIA        (LEN_CONFIG + LEN_IF_KBD + LEN_INTERFACE)
#define CFG_HID_IDX_SHARED      (LEN_CONFIG + LEN_IF_KBD + LEN_IF_VIA + LEN_INTERFACE)

/* 리포트 크기 */
#define HID_KBD_REPORT_SIZE     8     /* boot: mods + reserved + keys[6] */
#define VIA_REPORT_SIZE         32    /* raw HID IN/OUT */
#define SHARED_REPORT_SIZE      32    /* report-id + payload */

extern S_HSUSBD_INFO_T gsHSInfo;
extern uint8_t HID_KeyboardReportDescriptor[];
extern uint8_t HID_RawReportDescriptor[];
extern uint8_t HID_SharedReportDescriptor[];

#endif /* _USE_HW_USB */

#ifdef __cplusplus
}
#endif

#endif /* SRC_HW_DRIVER_USB_USB_HID_USBD_DESC_H_ */
