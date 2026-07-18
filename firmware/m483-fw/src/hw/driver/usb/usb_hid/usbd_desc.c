/*
 * usbd_desc.c
 *
 *  HSUSBD HID keyboard descriptor set (single boot-keyboard interface).
 *  Nuvoton BSP HSUSBD_HID_MouseKeyboard/descriptors.c 를 키보드 단일 인터페이스로 축약,
 *  bInterval 을 8K(High-Speed 125us) 기준인 1 로 설정.
 */

#include "usbd_desc.h"

#ifdef _USE_HW_USB


/*!< USB HID Keyboard Report Descriptor (standard 8-byte boot keyboard) */
uint8_t HID_KeyboardReportDescriptor[] __attribute__((aligned(4))) =
{
    0x05, 0x01,     /* Usage Page(Generic Desktop Controls) */
    0x09, 0x06,     /* Usage(Keyboard) */
    0xA1, 0x01,     /* Collection(Application) */
    0x05, 0x07,         /* Usage Page(Keyboard/Keypad) */
    0x19, 0xE0,         /* Usage Minimum(0xE0) */
    0x29, 0xE7,         /* Usage Maximum(0xE7) */
    0x15, 0x00,         /* Logical Minimum(0x0) */
    0x25, 0x01,         /* Logical Maximum(0x1) */
    0x75, 0x01,         /* Report Size(0x1) */
    0x95, 0x08,         /* Report Count(0x8) */
    0x81, 0x02,         /* Input (Data) => Modifier byte */
    0x95, 0x01,         /* Report Count(0x1) */
    0x75, 0x08,         /* Report Size(0x8) */
    0x81, 0x01,         /* Input (Constant) => Reserved byte */
    0x95, 0x05,         /* Report Count(0x5) */
    0x75, 0x01,         /* Report Size(0x1) */
    0x05, 0x08,         /* Usage Page(LEDs) */
    0x19, 0x01,         /* Usage Minimum(0x1) */
    0x29, 0x05,         /* Usage Maximum(0x5) */
    0x91, 0x02,         /* Output (Data) => LED report */
    0x95, 0x01,         /* Report Count(0x1) */
    0x75, 0x03,         /* Report Size(0x3) */
    0x91, 0x01,         /* Output (Constant) => LED report padding */
    0x95, 0x06,         /* Report Count(0x6) */
    0x75, 0x08,         /* Report Size(0x8) */
    0x15, 0x00,         /* Logical Minimum(0x0) */
    0x25, 0x65,         /* Logical Maximum(0x65) */
    0x05, 0x07,         /* Usage Page(Keyboard/Keypad) */
    0x19, 0x00,         /* Usage Minimum(0x0) */
    0x29, 0x65,         /* Usage Maximum(0x65) */
    0x81, 0x00,         /* Input (Data) => keycode[6] */
    0xC0            /* End Collection */
};


/*----------------------------------------------------------------------------*/
/*!< USB Device Descriptor */
uint8_t gu8DeviceDescriptor[] __attribute__((aligned(4))) =
{
    LEN_DEVICE,     /* bLength */
    DESC_DEVICE,    /* bDescriptorType */
    0x00, 0x02,     /* bcdUSB = 2.00 (high-speed capable) */
    0x00,           /* bDeviceClass */
    0x00,           /* bDeviceSubClass */
    0x00,           /* bDeviceProtocol */
    CEP_MAX_PKT_SIZE,   /* bMaxPacketSize0 */
    USBD_VID & 0x00FF,
    ((USBD_VID & 0xFF00) >> 8),
    USBD_PID & 0x00FF,
    ((USBD_PID & 0xFF00) >> 8),
    0x00, 0x00,     /* bcdDevice */
    0x01,           /* iManufacture */
    0x02,           /* iProduct */
    0x00,           /* iSerialNumber */
    0x01            /* bNumConfigurations */
};

/*!< USB Qualifier Descriptor (required for a proper high-speed device) */
uint8_t gu8QualifierDescriptor[] __attribute__((aligned(4))) =
{
    LEN_QUALIFIER,  /* bLength */
    DESC_QUALIFIER, /* bDescriptorType */
    0x00, 0x02,     /* bcdUSB */
    0x00,           /* bDeviceClass */
    0x00,           /* bDeviceSubClass */
    0x00,           /* bDeviceProtocol */
    CEP_OTHER_MAX_PKT_SIZE, /* bMaxPacketSize0 */
    0x01,           /* bNumConfigurations */
    0x00
};

/*!< USB Configuration Descriptor (High-Speed) */
uint8_t gu8ConfigDescriptor[] __attribute__((aligned(4))) =
{
    LEN_CONFIG,     /* bLength */
    DESC_CONFIG,    /* bDescriptorType */
    LEN_CONFIG_AND_SUBORDINATE & 0x00FF,
    ((LEN_CONFIG_AND_SUBORDINATE & 0xFF00) >> 8),
    0x01,           /* bNumInterfaces */
    0x01,           /* bConfigurationValue */
    0x00,           /* iConfiguration */
    0x80 | (USBD_SELF_POWERED << 6) | (USBD_REMOTE_WAKEUP << 5),
    USBD_MAX_POWER,

    /* Interface descriptor: HID - Keyboard */
    LEN_INTERFACE,  /* bLength */
    DESC_INTERFACE, /* bDescriptorType */
    0x00,           /* bInterfaceNumber */
    0x00,           /* bAlternateSetting */
    0x01,           /* bNumEndpoints */
    0x03,           /* bInterfaceClass : HID */
    0x01,           /* bInterfaceSubClass : boot */
    HID_KEYBOARD,   /* bInterfaceProtocol */
    0x00,           /* iInterface */

    /* HID descriptor */
    LEN_HID,        /* bLength */
    DESC_HID,       /* bDescriptorType */
    0x10, 0x01,     /* bcdHID */
    0x00,           /* bCountryCode */
    0x01,           /* bNumDescriptors */
    DESC_HID_RPT,   /* bDescriptorType (report) */
    sizeof(HID_KeyboardReportDescriptor) & 0x00FF,
    ((sizeof(HID_KeyboardReportDescriptor) & 0xFF00) >> 8),

    /* Endpoint descriptor: interrupt IN */
    LEN_ENDPOINT,   /* bLength */
    DESC_ENDPOINT,  /* bDescriptorType */
    (INT_IN_EP_NUM_KB | EP_INPUT),
    EP_INT,
    EPA_MAX_PKT_SIZE & 0x00FF,
    ((EPA_MAX_PKT_SIZE & 0xFF00) >> 8),
    HID_KBD_INT_IN_INTERVAL     /* bInterval = 1 => 8000 Hz (HS) */
};

/*!< USB Other-Speed Configuration Descriptor (reported while running High-Speed) */
uint8_t gu8OtherConfigDescriptorHS[] __attribute__((aligned(4))) =
{
    LEN_CONFIG,
    DESC_OTHERSPEED,
    LEN_CONFIG_AND_SUBORDINATE & 0x00FF,
    ((LEN_CONFIG_AND_SUBORDINATE & 0xFF00) >> 8),
    0x01,
    0x01,
    0x00,
    0x80 | (USBD_SELF_POWERED << 6) | (USBD_REMOTE_WAKEUP << 5),
    USBD_MAX_POWER,

    LEN_INTERFACE,
    DESC_INTERFACE,
    0x00,
    0x00,
    0x01,
    0x03,
    0x01,
    HID_KEYBOARD,
    0x00,

    LEN_HID,
    DESC_HID,
    0x10, 0x01,
    0x00,
    0x01,
    DESC_HID_RPT,
    sizeof(HID_KeyboardReportDescriptor) & 0x00FF,
    ((sizeof(HID_KeyboardReportDescriptor) & 0xFF00) >> 8),

    LEN_ENDPOINT,
    DESC_ENDPOINT,
    (INT_IN_EP_NUM_KB | EP_INPUT),
    EP_INT,
    EPA_OTHER_MAX_PKT_SIZE & 0x00FF,
    ((EPA_OTHER_MAX_PKT_SIZE & 0xFF00) >> 8),
    HID_KBD_INT_IN_INTERVAL
};

/*!< USB Configuration Descriptor (Full-Speed) */
uint8_t gu8ConfigDescriptorFS[] __attribute__((aligned(4))) =
{
    LEN_CONFIG,
    DESC_CONFIG,
    LEN_CONFIG_AND_SUBORDINATE & 0x00FF,
    ((LEN_CONFIG_AND_SUBORDINATE & 0xFF00) >> 8),
    0x01,
    0x01,
    0x00,
    0x80 | (USBD_SELF_POWERED << 6) | (USBD_REMOTE_WAKEUP << 5),
    USBD_MAX_POWER,

    LEN_INTERFACE,
    DESC_INTERFACE,
    0x00,
    0x00,
    0x01,
    0x03,
    0x01,
    HID_KEYBOARD,
    0x00,

    LEN_HID,
    DESC_HID,
    0x10, 0x01,
    0x00,
    0x01,
    DESC_HID_RPT,
    sizeof(HID_KeyboardReportDescriptor) & 0x00FF,
    ((sizeof(HID_KeyboardReportDescriptor) & 0xFF00) >> 8),

    LEN_ENDPOINT,
    DESC_ENDPOINT,
    (INT_IN_EP_NUM_KB | EP_INPUT),
    EP_INT,
    EPA_OTHER_MAX_PKT_SIZE & 0x00FF,
    ((EPA_OTHER_MAX_PKT_SIZE & 0xFF00) >> 8),
    HID_KBD_INT_IN_INTERVAL
};

/*!< USB Other-Speed Configuration Descriptor (reported while running Full-Speed) */
uint8_t gu8OtherConfigDescriptorFS[] __attribute__((aligned(4))) =
{
    LEN_CONFIG,
    DESC_OTHERSPEED,
    LEN_CONFIG_AND_SUBORDINATE & 0x00FF,
    ((LEN_CONFIG_AND_SUBORDINATE & 0xFF00) >> 8),
    0x01,
    0x01,
    0x00,
    0x80 | (USBD_SELF_POWERED << 6) | (USBD_REMOTE_WAKEUP << 5),
    USBD_MAX_POWER,

    LEN_INTERFACE,
    DESC_INTERFACE,
    0x00,
    0x00,
    0x01,
    0x03,
    0x01,
    HID_KEYBOARD,
    0x00,

    LEN_HID,
    DESC_HID,
    0x10, 0x01,
    0x00,
    0x01,
    DESC_HID_RPT,
    sizeof(HID_KeyboardReportDescriptor) & 0x00FF,
    ((sizeof(HID_KeyboardReportDescriptor) & 0xFF00) >> 8),

    LEN_ENDPOINT,
    DESC_ENDPOINT,
    (INT_IN_EP_NUM_KB | EP_INPUT),
    EP_INT,
    EPA_MAX_PKT_SIZE & 0x00FF,
    ((EPA_MAX_PKT_SIZE & 0xFF00) >> 8),
    HID_KBD_INT_IN_INTERVAL
};


/*!< USB Language String Descriptor */
uint8_t gu8StringLang[4] __attribute__((aligned(4))) =
{
    4,
    DESC_STRING,
    0x09, 0x04
};

/*!< USB Vendor String Descriptor */
uint8_t gu8VendorStringDesc[] __attribute__((aligned(4))) =
{
    16,
    DESC_STRING,
    'N', 0, 'u', 0, 'v', 0, 'o', 0, 't', 0, 'o', 0, 'n', 0
};

/*!< USB Product String Descriptor */
uint8_t gu8ProductStringDesc[] __attribute__((aligned(4))) =
{
    22,
    DESC_STRING,
    'M', 0, '4', 0, '8', 0, '3', 0, ' ', 0, 'K', 0, 'e', 0, 'y', 0, 's', 0, ' ', 0
};

uint8_t *gpu8UsbString[4] =
{
    gu8StringLang,
    gu8VendorStringDesc,
    gu8ProductStringDesc,
    NULL
};

/* One HID interface (keyboard), NULL terminated */
uint8_t *gu8UsbHidReport[2] =
{
    HID_KeyboardReportDescriptor,
    NULL
};

uint32_t gu32UsbHidReportLen[2] =
{
    sizeof(HID_KeyboardReportDescriptor),
    0,
};

/* Offset of the HID descriptor inside the configuration descriptor */
uint32_t gu32ConfigHidDescIdx[2] =
{
    (LEN_CONFIG + LEN_INTERFACE),
    0,
};

S_HSUSBD_INFO_T gsHSInfo =
{
    gu8DeviceDescriptor,
    gu8ConfigDescriptor,
    gpu8UsbString,
    gu8QualifierDescriptor,
    gu8ConfigDescriptorFS,
    gu8OtherConfigDescriptorHS,
    gu8OtherConfigDescriptorFS,
    gu8UsbHidReport,
    gu32UsbHidReportLen,
    gu32ConfigHidDescIdx,
};

#endif /* _USE_HW_USB */
