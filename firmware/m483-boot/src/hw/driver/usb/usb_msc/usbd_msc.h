/*
 * usbd_msc.h
 *
 *  USB Mass Storage Class (BOT / SCSI transparent) over HSUSBD, FULL-SPEED.
 *  Ported from Nuvoton M480 BSP HSUSBD_Mass_Storage_SRAM sample, with the
 *  media backend redirected:
 *    READ10  -> synthesized read-only FAT12/16 image (usbd_msc_disk).
 *    WRITE10 -> received inline (whole data phase per mscProcess() call) and
 *               each 512-byte sector handed to the mscSetWriteCb() callback
 *               (ap.c -> uf2_write_block). No ring / producer-consumer.
 */
#ifndef SRC_HW_DRIVER_USB_USB_MSC_USBD_MSC_H_
#define SRC_HW_DRIVER_USB_USB_MSC_USBD_MSC_H_

#ifdef __cplusplus
extern "C" {
#endif

#include "hw_def.h"

/*---------------------------------------------------------------------------
 * Geometry / EP configuration (FULL-SPEED : 64-byte bulk packets)
 *-------------------------------------------------------------------------*/
/* Define DMA Maximum Transfer length */
#define USBD_MAX_DMA_LEN                0x1000

/* Define sector size */
#define USBD_SECTOR_SIZE               512

/* Define EP maximum packet size (FS) */
#define CEP_MAX_PKT_SIZE               64
#define CEP_OTHER_MAX_PKT_SIZE         64
#define EPA_MAX_PKT_SIZE               64      /* bulk-IN  FS */
#define EPA_OTHER_MAX_PKT_SIZE         64
#define EPB_MAX_PKT_SIZE               64      /* bulk-OUT FS */
#define EPB_OTHER_MAX_PKT_SIZE         64

#define CEP_BUF_BASE                   0
#define CEP_BUF_LEN                    CEP_MAX_PKT_SIZE
#define EPA_BUF_BASE                   0x200
#define EPA_BUF_LEN                    0x200
#define EPB_BUF_BASE                   0x400
#define EPB_BUF_LEN                    0x200

/* Define the bulk EP numbers (bulk-IN = 0x81, bulk-OUT = 0x01) */
#define BULK_IN_EP_NUM                 0x01
#define BULK_OUT_EP_NUM                0x01

/* Descriptor information */
#define USBD_SELF_POWERED              0
#define USBD_REMOTE_WAKEUP             0
#define USBD_MAX_POWER                 50      /* unit 2mA -> 100mA */

/*---------------------------------------------------------------------------
 * Mass Storage class-specific requests
 *-------------------------------------------------------------------------*/
#define BULK_ONLY_MASS_STORAGE_RESET   0xFF
#define GET_MAX_LUN                    0xFE

/* CBW / CSW signatures */
#define CBW_SIGNATURE                  0x43425355
#define CSW_SIGNATURE                  0x53425355

/*---------------------------------------------------------------------------
 * SCSI / UFI command op-codes
 *-------------------------------------------------------------------------*/
#define UFI_TEST_UNIT_READY                     0x00
#define UFI_REQUEST_SENSE                       0x03
#define UFI_INQUIRY                             0x12
#define UFI_MODE_SELECT_6                       0x15
#define UFI_MODE_SENSE_6                        0x1A
#define UFI_START_STOP                          0x1B
#define UFI_PREVENT_ALLOW_MEDIUM_REMOVAL        0x1E
#define UFI_READ_FORMAT_CAPACITY                0x23
#define UFI_READ_CAPACITY                       0x25
#define UFI_READ_10                             0x28
#define UFI_WRITE_10                            0x2A
#define UFI_VERIFY_10                           0x2F
#define UFI_MODE_SELECT_10                      0x55
#define UFI_MODE_SENSE_10                       0x5A

/* BOT state machine */
#define BULK_CBW                       0x00
#define BULK_IN                        0x01
#define BULK_OUT                       0x02
#define BULK_CSW                       0x04
#define BULK_NORMAL                    0xFF

static __INLINE uint32_t get_be32(uint8_t *buf)
{
  return ((uint32_t) buf[0] << 24) | ((uint32_t) buf[1] << 16) |
         ((uint32_t) buf[2] << 8)  | ((uint32_t) buf[3]);
}

/*---------------------------------------------------------------------------
 * BOT wrappers
 *-------------------------------------------------------------------------*/
struct CBW
{
  uint32_t  dCBWSignature;
  uint32_t  dCBWTag;
  uint32_t  dCBWDataTransferLength;
  uint8_t   bmCBWFlags;
  uint8_t   bCBWLUN;
  uint8_t   bCBWCBLength;
  uint8_t   u8OPCode;
  uint8_t   u8LUN;
  uint8_t   au8Data[14];
};

struct CSW
{
  uint32_t  dCSWSignature;
  uint32_t  dCSWTag;
  uint32_t  dCSWDataResidue;
  uint8_t   bCSWStatus;
};

/*---------------------------------------------------------------------------
 * State shared with the HSUSBD IRQ handler (usbd_conf.c)
 *
 * Event-driven BOT reception (mirrors the BSP DataFlash MassStorage.c):
 *   - The EPB (bulk-OUT) RXPKIF ISR sets g_u8MscOutPacket = 1 when an OUT
 *     packet lands and disables the EPB interrupt; the OUT-DMA-done branch of
 *     the IRQ re-enables EPB RXPKIEN.
 *   - mscProcess() only receives a CBW / WRITE10 sector when that flag is set.
 *-------------------------------------------------------------------------*/
extern uint8_t volatile g_u8MscOutPacket;  /* OUT packet arrived on EPB        */
extern uint8_t          g_u8BulkState;     /* BOT state (BULK_CBW only)        */

/*---------------------------------------------------------------------------
 * WRITE10 sink callback.
 *
 * mscProcess() runs in the MAIN LOOP (via usbUpdate()), never in the USB IRQ,
 * so the whole WRITE10 data phase is received inline and each 512-byte sector
 * is handed to this callback before the CSW is returned. ap.c registers a
 * callback that calls uf2_write_block(), keeping the USB layer decoupled from
 * uf2. Return value is currently ignored (reserved for future error status).
 *-------------------------------------------------------------------------*/
typedef int (*msc_write_cb_t)(uint32_t lba, uint8_t *data);
void mscSetWriteCb(msc_write_cb_t cb);

/* 호스트가 "안전하게 제거(꺼내기)"(START STOP UNIT + LOEJ)를 보냈으면 true.
 * 한 번 set되면 유지된다(리셋으로만 클리어). ap.c 메인루프에서 폴링. */
bool mscEjectRequested(void);

/*---------------------------------------------------------------------------
 * Public API
 *-------------------------------------------------------------------------*/
void mscInit(void);            /* endpoint config + BOT/SCSI state            */
void mscProcess(void);         /* polled BOT/SCSI worker (main loop)          */
void mscClassRequest(void);    /* HSUSBD_Open class-request callback          */

/* Endpoint (re)configuration hooks used by the IRQ bus-reset path. */
void mscInitForFullSpeed(void);
void mscInitForHighSpeed(void);
void mscOnBusReset(void);      /* flush/reset MSC EPs + state on USB reset    */

#ifdef __cplusplus
}
#endif

#endif /* SRC_HW_DRIVER_USB_USB_MSC_USBD_MSC_H_ */
