/*
 * usbd_desc.c
 *
 *  USB descriptors for the full-speed MSC (BOT / SCSI transparent) bootloader.
 *  Ported from the Nuvoton M480 BSP HSUSBD MSC sample, trimmed to full-speed
 *  and re-identified with the bootloader VID/PID and product string.
 *
 *    Device class  = 0x00 (defined at interface)
 *    Interface     = 0x08 / 0x06 / 0x50  (Mass Storage / SCSI transparent / BOT)
 *    Endpoints     = bulk-IN 0x81, bulk-OUT 0x01, 64-byte (FS)
 */
#include "NuMicro.h"
#include "usbd_msc.h"
#include "usbd_desc.h"
#include "uf2/uf2_def.h"

/*----------------------------------------------------------------------------*/
/* Device Descriptor */
uint8_t gu8DeviceDescriptor[] __attribute__((aligned(4))) =
{
  LEN_DEVICE,         /* bLength            */
  DESC_DEVICE,        /* bDescriptorType    */
  0x00, 0x02,         /* bcdUSB    2.00     */
  0x00,               /* bDeviceClass       */
  0x00,               /* bDeviceSubClass    */
  0x00,               /* bDeviceProtocol    */
  CEP_MAX_PKT_SIZE,   /* bMaxPacketSize0    */
  USB_VID & 0x00FF,           /* idVendor  */
  (USB_VID & 0xFF00) >> 8,
  USB_PID & 0x00FF,           /* idProduct */
  (USB_PID & 0xFF00) >> 8,
  0x00, 0x01,         /* bcdDevice 1.00     */
  0x01,               /* iManufacture       */
  0x02,               /* iProduct           */
  0x03,               /* iSerialNumber      */
  0x01                /* bNumConfigurations */
};

/* Qualifier Descriptor */
uint8_t gu8QualifierDescriptor[] __attribute__((aligned(4))) =
{
  LEN_QUALIFIER,      /* bLength            */
  DESC_QUALIFIER,     /* bDescriptorType    */
  0x00, 0x02,         /* bcdUSB             */
  0x00,               /* bDeviceClass       */
  0x00,               /* bDeviceSubClass    */
  0x00,               /* bDeviceProtocol    */
  CEP_OTHER_MAX_PKT_SIZE, /* bMaxPacketSize0 */
  0x01,               /* bNumConfigurations */
  0x00
};

/*----------------------------------------------------------------------------*/
/* One MSC interface : bulk-IN + bulk-OUT. Macro so every config/other-speed
 * block stays consistent; ep_in / ep_out packet sizes are the only variable. */
#define MSC_INTERFACE_BLOCK(_desc_type, _in_pkt, _out_pkt)               \
  LEN_CONFIG,                                                            \
  (_desc_type),                                                          \
  (LEN_CONFIG + LEN_INTERFACE + LEN_ENDPOINT * 2), 0x00, /* wTotalLen */ \
  0x01,               /* bNumInterfaces    */                           \
  0x01,               /* bConfigurationValue */                         \
  0x00,               /* iConfiguration    */                           \
  0x80 | (USBD_SELF_POWERED << 6) | (USBD_REMOTE_WAKEUP << 5),          \
  USBD_MAX_POWER,                                                        \
  /* Interface */                                                        \
  LEN_INTERFACE,                                                         \
  DESC_INTERFACE,                                                        \
  0x00,               /* bInterfaceNumber  */                           \
  0x00,               /* bAlternateSetting */                           \
  0x02,               /* bNumEndpoints     */                           \
  0x08,               /* bInterfaceClass    : Mass Storage      */       \
  0x06,               /* bInterfaceSubClass : SCSI transparent  */       \
  0x50,               /* bInterfaceProtocol : Bulk-Only (BOT)   */       \
  0x00,               /* iInterface        */                           \
  /* EP : bulk-IN */                                                     \
  LEN_ENDPOINT,                                                          \
  DESC_ENDPOINT,                                                         \
  (BULK_IN_EP_NUM | EP_INPUT),                                           \
  EP_BULK,                                                               \
  (_in_pkt) & 0x00FF, ((_in_pkt) & 0xFF00) >> 8,                         \
  0x00,               /* bInterval         */                           \
  /* EP : bulk-OUT */                                                    \
  LEN_ENDPOINT,                                                          \
  DESC_ENDPOINT,                                                         \
  (BULK_OUT_EP_NUM | EP_OUTPUT),                                         \
  EP_BULK,                                                               \
  (_out_pkt) & 0x00FF, ((_out_pkt) & 0xFF00) >> 8,                       \
  0x00                /* bInterval         */

/* Active configuration : we enumerate full-speed -> 64-byte bulk. */
uint8_t gu8ConfigDescriptor[] __attribute__((aligned(4))) =
{
  MSC_INTERFACE_BLOCK(DESC_CONFIG, EPA_MAX_PKT_SIZE, EPB_MAX_PKT_SIZE)
};

/* Full-speed configuration (used when current speed is HS) */
uint8_t gu8ConfigDescriptorFS[] __attribute__((aligned(4))) =
{
  MSC_INTERFACE_BLOCK(DESC_CONFIG, EPA_OTHER_MAX_PKT_SIZE, EPB_OTHER_MAX_PKT_SIZE)
};

/* Other-speed (HS) configuration */
uint8_t gu8OtherConfigDescriptorHS[] __attribute__((aligned(4))) =
{
  MSC_INTERFACE_BLOCK(DESC_OTHERSPEED, EPA_MAX_PKT_SIZE, EPB_MAX_PKT_SIZE)
};

/* Other-speed (FS) configuration */
uint8_t gu8OtherConfigDescriptorFS[] __attribute__((aligned(4))) =
{
  MSC_INTERFACE_BLOCK(DESC_OTHERSPEED, EPA_OTHER_MAX_PKT_SIZE, EPB_OTHER_MAX_PKT_SIZE)
};

/*----------------------------------------------------------------------------*/
/* String descriptors */
uint8_t gu8StringLang[4] __attribute__((aligned(4))) =
{
  4, DESC_STRING, 0x09, 0x04
};

/* Manufacturer : "WISH" */
uint8_t gu8VendorStringDesc[] __attribute__((aligned(4))) =
{
  10, DESC_STRING,
  'W', 0, 'I', 0, 'S', 0, 'H', 0
};

/* Product : UF2_PRODUCT_NAME ("WISH-BOOT") */
uint8_t gu8ProductStringDesc[] __attribute__((aligned(4))) =
{
  20, DESC_STRING,
  'W', 0, 'I', 0, 'S', 0, 'H', 0, '-', 0, 'B', 0, 'O', 0, 'O', 0, 'T', 0
};

/* Serial number */
uint8_t gu8StringSerial[] __attribute__((aligned(4))) =
{
  22, DESC_STRING,
  'W', 0, 'I', 0, 'S', 0, 'H', 0, 'B', 0, 'O', 0, 'O', 0, 'T', 0, '0', 0, '1', 0
};

uint8_t *gpu8UsbString[4] =
{
  gu8StringLang,
  gu8VendorStringDesc,
  gu8ProductStringDesc,
  gu8StringSerial
};

/* MSC has no HID report descriptors */
static uint8_t *gu8UsbHidReport[3]      = { NULL, NULL, NULL };
static uint32_t gu32UsbHidReportLen[3]  = { 0, 0, 0 };
static uint32_t gu32ConfigHidDescIdx[3] = { 0, 0, 0 };

/* HSUSBD info table (name matches the extern in hsusbd.h) */
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
