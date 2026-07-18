/*
 * usbd_desc.c
 *
 *  HSUSBD HID 복합장치 디스크립터 (QMK 네이티브 3-인터페이스).
 *   IF0 Keyboard(boot 6KRO)  : HID_KeyboardReportDescriptor  -> EPA IN
 *   IF1 Raw/VIA(0xFF60)       : HID_RawReportDescriptor       -> EPB IN / EPC OUT
 *   IF2 Shared(NKRO/sys/cons) : HID_SharedReportDescriptor    -> EPD IN
 */

#include "usbd_desc.h"

#ifdef _USE_HW_USB


/*!< IF0: 표준 8바이트 boot 키보드 (report-id 없음) */
uint8_t HID_KeyboardReportDescriptor[] __attribute__((aligned(4))) =
{
    0x05, 0x01,     /* Usage Page(Generic Desktop) */
    0x09, 0x06,     /* Usage(Keyboard) */
    0xA1, 0x01,     /* Collection(Application) */
    0x05, 0x07,         /* Usage Page(Keyboard/Keypad) */
    0x19, 0xE0,         /* Usage Minimum(0xE0) */
    0x29, 0xE7,         /* Usage Maximum(0xE7) */
    0x15, 0x00,
    0x25, 0x01,
    0x75, 0x01,         /* Report Size(1) */
    0x95, 0x08,         /* Report Count(8) */
    0x81, 0x02,         /* Input(Data,Var,Abs) => Modifier byte */
    0x95, 0x01,
    0x75, 0x08,
    0x81, 0x01,         /* Input(Constant) => Reserved byte */
    0x95, 0x05,
    0x75, 0x01,
    0x05, 0x08,         /* Usage Page(LEDs) */
    0x19, 0x01,
    0x29, 0x05,
    0x91, 0x02,         /* Output(Data,Var,Abs) => LED report */
    0x95, 0x01,
    0x75, 0x03,
    0x91, 0x01,         /* Output(Constant) => LED padding */
    0x95, 0x06,
    0x75, 0x08,
    0x15, 0x00,
    0x25, 0x65,
    0x05, 0x07,
    0x19, 0x00,
    0x29, 0x65,
    0x81, 0x00,         /* Input(Data,Array) => keycode[6] */
    0xC0            /* End Collection */
};

/*!< IF1: Raw HID / VIA (Vendor 0xFF60, 32바이트 IN/OUT, report-id 없음) */
uint8_t HID_RawReportDescriptor[] __attribute__((aligned(4))) =
{
    0x06, 0x60, 0xFF,   /* Usage Page(Vendor 0xFF60) */
    0x09, 0x61,         /* Usage(0x61) */
    0xA1, 0x01,         /* Collection(Application) */
    0x09, 0x62,             /* Usage(0x62) : data to host */
    0x15, 0x00,
    0x26, 0xFF, 0x00,       /* Logical Maximum(255) */
    0x95, VIA_REPORT_SIZE,  /* Report Count(32) */
    0x75, 0x08,             /* Report Size(8) */
    0x81, 0x02,             /* Input(Data,Var,Abs) */
    0x09, 0x63,             /* Usage(0x63) : data from host */
    0x15, 0x00,
    0x26, 0xFF, 0x00,
    0x95, VIA_REPORT_SIZE,
    0x75, 0x08,
    0x91, 0x02,             /* Output(Data,Var,Abs) */
    0xC0            /* End Collection */
};

/*!< IF2: Shared - System Control(id3) + Consumer(id4) + NKRO(id6) */
uint8_t HID_SharedReportDescriptor[] __attribute__((aligned(4))) =
{
    /* System Control (report id 3) */
    0x05, 0x01,         /* Usage Page(Generic Desktop) */
    0x09, 0x80,         /* Usage(System Control) */
    0xA1, 0x01,         /* Collection(Application) */
    0x85, 0x03,             /* Report ID(3) */
    0x19, 0x01,             /* Usage Minimum(1) */
    0x2A, 0xB7, 0x00,       /* Usage Maximum(0x00B7) */
    0x15, 0x01,
    0x26, 0xB7, 0x00,
    0x95, 0x01,             /* Report Count(1) */
    0x75, 0x10,             /* Report Size(16) */
    0x81, 0x00,             /* Input(Data,Array) */
    0xC0,               /* End Collection */

    /* Consumer Control (report id 4) */
    0x05, 0x0C,         /* Usage Page(Consumer) */
    0x09, 0x01,         /* Usage(Consumer Control) */
    0xA1, 0x01,         /* Collection(Application) */
    0x85, 0x04,             /* Report ID(4) */
    0x19, 0x01,
    0x2A, 0xA0, 0x02,       /* Usage Maximum(0x02A0) */
    0x15, 0x01,
    0x26, 0xA0, 0x02,
    0x95, 0x01,
    0x75, 0x10,
    0x81, 0x00,
    0xC0,               /* End Collection */

    /* NKRO Keyboard (report id 6) : mods(8) + bits(240) */
    0x05, 0x01,         /* Usage Page(Generic Desktop) */
    0x09, 0x06,         /* Usage(Keyboard) */
    0xA1, 0x01,         /* Collection(Application) */
    0x85, 0x06,             /* Report ID(6) */
    0x05, 0x07,             /* Usage Page(Keyboard/Keypad) */
    0x19, 0xE0,
    0x29, 0xE7,
    0x15, 0x00,
    0x25, 0x01,
    0x95, 0x08,             /* Report Count(8) */
    0x75, 0x01,             /* Report Size(1) */
    0x81, 0x02,             /* Input(Data,Var,Abs) => mods */
    0x05, 0x07,
    0x19, 0x00,
    0x29, 0xEF,             /* Usage Maximum(239) */
    0x15, 0x00,
    0x25, 0x01,
    0x95, 0xF0,             /* Report Count(240) */
    0x75, 0x01,
    0x81, 0x02,             /* Input(Data,Var,Abs) => key bits */
    0xC0,               /* End Collection */

    /* Mouse (report id 2) : buttons(5) + X/Y(int8) + wheel(V) + pan(H) */
    0x05, 0x01,         /* Usage Page(Generic Desktop) */
    0x09, 0x02,         /* Usage(Mouse) */
    0xA1, 0x01,         /* Collection(Application) */
    0x85, 0x02,             /* Report ID(2) */
    0x09, 0x01,             /* Usage(Pointer) */
    0xA1, 0x00,             /* Collection(Physical) */
    0x05, 0x09,                 /* Usage Page(Button) */
    0x19, 0x01,
    0x29, 0x05,                 /* buttons 1..5 */
    0x15, 0x00,
    0x25, 0x01,
    0x95, 0x05,                 /* Report Count(5) */
    0x75, 0x01,                 /* Report Size(1) */
    0x81, 0x02,                 /* Input(Data,Var,Abs) => buttons */
    0x95, 0x01,
    0x75, 0x03,
    0x81, 0x01,                 /* Input(Const) => padding */
    0x05, 0x01,                 /* Usage Page(Generic Desktop) */
    0x09, 0x30,                 /* Usage(X) */
    0x09, 0x31,                 /* Usage(Y) */
    0x15, 0x81,                 /* Logical Min(-127) */
    0x25, 0x7F,                 /* Logical Max(127) */
    0x75, 0x08,
    0x95, 0x02,                 /* Report Count(2) => X,Y */
    0x81, 0x06,                 /* Input(Data,Var,Rel) */
    0x09, 0x38,                 /* Usage(Wheel) */
    0x15, 0x81,
    0x25, 0x7F,
    0x75, 0x08,
    0x95, 0x01,                 /* Report Count(1) => V wheel */
    0x81, 0x06,                 /* Input(Data,Var,Rel) */
    0x05, 0x0C,                 /* Usage Page(Consumer) */
    0x0A, 0x38, 0x02,           /* Usage(AC Pan) */
    0x15, 0x81,
    0x25, 0x7F,
    0x75, 0x08,
    0x95, 0x01,                 /* Report Count(1) => H pan */
    0x81, 0x06,                 /* Input(Data,Var,Rel) */
    0xC0,               /* End Collection(Physical) */
    0xC0            /* End Collection(Application) */
};


/*----------------------------------------------------------------------------*/
/*!< USB Device Descriptor */
uint8_t gu8DeviceDescriptor[] __attribute__((aligned(4))) =
{
    LEN_DEVICE,     /* bLength */
    DESC_DEVICE,    /* bDescriptorType */
    0x00, 0x02,     /* bcdUSB = 2.00 */
    0x00,           /* bDeviceClass */
    0x00,           /* bDeviceSubClass */
    0x00,           /* bDeviceProtocol */
    CEP_MAX_PKT_SIZE,
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

/*!< USB Qualifier Descriptor */
uint8_t gu8QualifierDescriptor[] __attribute__((aligned(4))) =
{
    LEN_QUALIFIER,
    DESC_QUALIFIER,
    0x00, 0x02,
    0x00,
    0x00,
    0x00,
    CEP_OTHER_MAX_PKT_SIZE,
    0x01,
    0x00
};


/*
 * Config descriptor 본문(3 인터페이스). HS/FS/OtherSpeed 4벌이 동일하고
 * 첫 bDescriptorType(DESC_CONFIG vs DESC_OTHERSPEED)만 다르므로 매크로로 공유한다.
 * 인터럽트 EP 는 payload<=64 라 FS 에서도 wMaxPacketSize 동일.
 */
#define HID_CONFIG_DESC(cfgType)                                                    \
    LEN_CONFIG,     /* bLength */                                                   \
    cfgType,        /* bDescriptorType */                                          \
    LEN_CONFIG_AND_SUBORDINATE & 0x00FF,                                            \
    ((LEN_CONFIG_AND_SUBORDINATE & 0xFF00) >> 8),                                   \
    NUM_INTERFACES, /* bNumInterfaces */                                            \
    0x01,           /* bConfigurationValue */                                       \
    0x00,           /* iConfiguration */                                            \
    0x80 | (USBD_SELF_POWERED << 6) | (USBD_REMOTE_WAKEUP << 5),                     \
    USBD_MAX_POWER,                                                                  \
                                                                                    \
    /* ---- IF0 : HID Keyboard (boot) ---- */                                       \
    LEN_INTERFACE, DESC_INTERFACE, IF_NUM_KBD, 0x00, 0x01,                          \
    0x03, 0x01, HID_KEYBOARD, 0x00,                                                 \
    LEN_HID, DESC_HID, 0x10, 0x01, 0x00, 0x01, DESC_HID_RPT,                        \
    sizeof(HID_KeyboardReportDescriptor) & 0x00FF,                                  \
    ((sizeof(HID_KeyboardReportDescriptor) & 0xFF00) >> 8),                         \
    LEN_ENDPOINT, DESC_ENDPOINT, (INT_IN_EP_NUM_KB | EP_INPUT), EP_INT,             \
    EPA_MAX_PKT_SIZE & 0x00FF, ((EPA_MAX_PKT_SIZE & 0xFF00) >> 8),                  \
    HID_KBD_INT_IN_INTERVAL,                                                        \
                                                                                    \
    /* ---- IF1 : Raw/VIA (IN + OUT) ---- */                                        \
    LEN_INTERFACE, DESC_INTERFACE, IF_NUM_VIA, 0x00, 0x02,                          \
    0x03, 0x00, HID_NONE, 0x00,                                                     \
    LEN_HID, DESC_HID, 0x10, 0x01, 0x00, 0x01, DESC_HID_RPT,                        \
    sizeof(HID_RawReportDescriptor) & 0x00FF,                                       \
    ((sizeof(HID_RawReportDescriptor) & 0xFF00) >> 8),                              \
    LEN_ENDPOINT, DESC_ENDPOINT, (INT_IN_EP_NUM_VIA | EP_INPUT), EP_INT,            \
    EPB_MAX_PKT_SIZE & 0x00FF, ((EPB_MAX_PKT_SIZE & 0xFF00) >> 8),                  \
    HID_VIA_INT_INTERVAL,                                                           \
    LEN_ENDPOINT, DESC_ENDPOINT, (OUT_EP_NUM_VIA | EP_OUTPUT), EP_INT,              \
    EPC_MAX_PKT_SIZE & 0x00FF, ((EPC_MAX_PKT_SIZE & 0xFF00) >> 8),                  \
    HID_VIA_INT_INTERVAL,                                                           \
                                                                                    \
    /* ---- IF2 : Shared (NKRO/system/consumer) ---- */                            \
    LEN_INTERFACE, DESC_INTERFACE, IF_NUM_SHARED, 0x00, 0x01,                       \
    0x03, 0x00, HID_NONE, 0x00,                                                     \
    LEN_HID, DESC_HID, 0x10, 0x01, 0x00, 0x01, DESC_HID_RPT,                        \
    sizeof(HID_SharedReportDescriptor) & 0x00FF,                                    \
    ((sizeof(HID_SharedReportDescriptor) & 0xFF00) >> 8),                           \
    LEN_ENDPOINT, DESC_ENDPOINT, (INT_IN_EP_NUM_SHARED | EP_INPUT), EP_INT,         \
    EPD_MAX_PKT_SIZE & 0x00FF, ((EPD_MAX_PKT_SIZE & 0xFF00) >> 8),                  \
    HID_SHARED_INT_IN_INTERVAL

uint8_t gu8ConfigDescriptor[]        __attribute__((aligned(4))) = { HID_CONFIG_DESC(DESC_CONFIG) };
uint8_t gu8OtherConfigDescriptorHS[] __attribute__((aligned(4))) = { HID_CONFIG_DESC(DESC_OTHERSPEED) };
uint8_t gu8ConfigDescriptorFS[]      __attribute__((aligned(4))) = { HID_CONFIG_DESC(DESC_CONFIG) };
uint8_t gu8OtherConfigDescriptorFS[] __attribute__((aligned(4))) = { HID_CONFIG_DESC(DESC_OTHERSPEED) };


/*!< String Descriptors */
uint8_t gu8StringLang[4] __attribute__((aligned(4))) =
{
    4, DESC_STRING, 0x09, 0x04
};

uint8_t gu8VendorStringDesc[] __attribute__((aligned(4))) =
{
    16, DESC_STRING,
    'N', 0, 'u', 0, 'v', 0, 'o', 0, 't', 0, 'o', 0, 'n', 0
};

uint8_t gu8ProductStringDesc[] __attribute__((aligned(4))) =
{
    20, DESC_STRING,
    'W', 0, 'I', 0, 'S', 0, 'H', 0, '4', 0, '5', 0, '-', 0, '8', 0, 'K', 0
};

uint8_t *gpu8UsbString[4] =
{
    gu8StringLang,
    gu8VendorStringDesc,
    gu8ProductStringDesc,
    NULL
};

/* 인터페이스 순서와 동일하게 report 디스크립터 배열 (NULL 종단) */
uint8_t *gu8UsbHidReport[NUM_INTERFACES + 1] =
{
    HID_KeyboardReportDescriptor,
    HID_RawReportDescriptor,
    HID_SharedReportDescriptor,
    NULL
};

uint32_t gu32UsbHidReportLen[NUM_INTERFACES + 1] =
{
    sizeof(HID_KeyboardReportDescriptor),
    sizeof(HID_RawReportDescriptor),
    sizeof(HID_SharedReportDescriptor),
    0,
};

uint32_t gu32ConfigHidDescIdx[NUM_INTERFACES + 1] =
{
    CFG_HID_IDX_KBD,
    CFG_HID_IDX_VIA,
    CFG_HID_IDX_SHARED,
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
