/*
 * usbd_msc.c
 *
 *  BOT / SCSI-transparent Mass Storage state machine over HSUSBD, full-speed.
 *  Ported from the Nuvoton M480 BSP HSUSBD_Mass_Storage_SRAM sample, with the
 *  media backend replaced:
 *
 *    READ10  -> vdiskRead()  (synthesized read-only FAT12 image, RAM/const).
 *    WRITE10 -> the ENTIRE data phase is received inline inside the CBW
 *               dispatch (one mscProcess() call), exactly like the BSP
 *               HSUSBD_Mass_Storage_DataFlash MSC_ProcessCmd WRITE_10 case:
 *               each 512-byte sector is DMA'd into a static buffer and passed
 *               to the registered write callback (mscSetWriteCb) before the
 *               CSW is returned. No ring / producer-consumer.
 *
 *  mscProcess() is polled from the MAIN LOOP (usbUpdate()), never from the USB
 *  IRQ, so the write callback (uf2_write_block via ap.c) runs off the IRQ path.
 *  The HSUSBD IRQ (usbd_conf.c) only services the control pipe, bus events,
 *  DMA-done signalling and the EPB OUT-packet-arrived flag.
 */
#include <string.h>

#include "NuMicro.h"
#include "usbd_msc.h"
#include "usbd_msc_disk.h"
#include "log.h"

#ifndef MSC_TRACE_CBW
#define MSC_TRACE_CBW   0     /* 1: CBW opcode 트레이스(디버그), 0: 끔 */
#endif

/*--------------------------------------------------------------------------*/
/* Flow-control / SCSI state                                                */
/*--------------------------------------------------------------------------*/
int32_t  g_TotalSectors = 0;

uint8_t volatile g_u8MscOutPacket = 0;   /* set by EPB RXPKIF ISR */
uint8_t          g_u8BulkState    = BULK_CBW;
uint8_t          g_u8Prevent      = 0;
uint8_t          g_au8SenseKey[4];

uint32_t g_u32MSCMaxLun = 0;
uint32_t g_u32LbaAddress;
uint32_t g_u32MassBase;              /* -> mscCmdBuf, DMA scratch for CBW/CSW/responses */
uint32_t g_u32EpMaxPacketSize;
uint32_t g_u32CbwSize = 0;           /* actual received CBW length (EPDATCNT)          */

struct CBW g_sCBW;
struct CSW g_sCSW;

/* DMA-capable scratch buffer for command responses / CBW / CSW. */
static uint8_t mscCmdBuf[512]  __attribute__((aligned(4)));
/* DMA-capable scratch buffer used to serve one READ10 sector from vdisk. */
static uint8_t mscReadBuf[512] __attribute__((aligned(4)));
/* DMA-capable scratch buffer used to receive one WRITE10 sector. */
static uint8_t mscWriteBuf[512] __attribute__((aligned(4)));

/* WRITE10 sink (registered by ap.c -> uf2_write_block). NULL = discard. */
static msc_write_cb_t g_msc_write_cb = NULL;

void mscSetWriteCb(msc_write_cb_t cb)
{
  g_msc_write_cb = cb;
}

/* 호스트의 "안전하게 제거(꺼내기)" 래치. START STOP UNIT(0x1B)에 LOEJ 비트가
 * 실려 오면 set. 메인루프(ap.c)가 CSW 전송 후 이를 보고 부트 종료를 결정. */
static volatile uint8_t g_msc_eject = 0;

bool mscEjectRequested(void)
{
  return g_msc_eject != 0;
}

/*--------------------------------------------------------------------------*/
/* Inquiry / mode-sense tables                                               */
/*--------------------------------------------------------------------------*/
static uint8_t g_au8InquiryID[36] =
{
  0x00,                   /* Peripheral Device Type : direct-access block   */
  0x80,                   /* RMB : removable                                */
  0x02,                   /* Version : SCSI-2 (SPC) — 0 이면 macOS가 dumb 장치로
                           * 취급해 START_STOP eject 핸드셰이크를 생략함        */
  0x02,                   /* Response Data Format : 2 (표준 준수). TinyUSB 동일  */
  0x1F, 0x00, 0x00, 0x00, /* Additional Length (31)                          */
  /* Vendor Identification (8) */
  'W', 'I', 'S', 'H', ' ', ' ', ' ', ' ',
  /* Product Identification (16) */
  'U', 'F', '2', ' ', 'B', 'o', 'o', 't', 'l', 'o', 'a', 'd', 'e', 'r', ' ', ' ',
  /* Product Revision (4) */
  '1', '.', '0', '0'
};

static uint8_t g_au8ModePage_01[12] =
{
  0x01, 0x0A, 0x00, 0x03, 0x00, 0x00, 0x00, 0x00,
  0x03, 0x00, 0x00, 0x00
};

static uint8_t g_au8ModePage_05[32] =
{
  0x05, 0x1E, 0x13, 0x88, 0x08, 0x20, 0x02, 0x00,
  0x01, 0xF4, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
  0x00, 0x00, 0x00, 0x05, 0x1E, 0x00, 0x00, 0x00,
  0x00, 0x00, 0x00, 0x00, 0x01, 0x68, 0x00, 0x00
};

static uint8_t g_au8ModePage_1B[12] =
{
  0x1B, 0x0A, 0x00, 0x01, 0x00, 0x00, 0x00, 0x00,
  0x00, 0x00, 0x00, 0x00
};

static uint8_t g_au8ModePage_1C[8] =
{
  0x1C, 0x06, 0x00, 0x05, 0x00, 0x00, 0x00, 0x00
};

static uint8_t g_au8ModePage[24] =
{
  0x03, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
  0x00, 0x00, 0x02, 0x00, 0x1C, 0x0A, 0x80, 0x03,
  0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x01
};

/*--------------------------------------------------------------------------*/
/* Forward declarations (internal)                                           */
/*--------------------------------------------------------------------------*/
static void MSC_ActiveDMA(uint32_t u32Addr, uint32_t u32Len);
static void MSC_BulkIn(uint32_t u32Addr, uint32_t u32Len);
static void MSC_ReceiveCBW(uint32_t u32Buf, uint32_t u32Len);
static void MSC_AckCmd(void);

/*--------------------------------------------------------------------------*/
/* Endpoint configuration                                                    */
/*--------------------------------------------------------------------------*/
void mscInitForHighSpeed(void)
{
  /* EPA ==> Bulk IN endpoint */
  HSUSBD_SetEpBufAddr(EPA, EPA_BUF_BASE, EPA_BUF_LEN);
  HSUSBD_SET_MAX_PAYLOAD(EPA, EPA_MAX_PKT_SIZE);
  HSUSBD_ConfigEp(EPA, BULK_IN_EP_NUM, HSUSBD_EP_CFG_TYPE_BULK, HSUSBD_EP_CFG_DIR_IN);

  /* EPB ==> Bulk OUT endpoint */
  HSUSBD_SetEpBufAddr(EPB, EPB_BUF_BASE, EPB_BUF_LEN);
  HSUSBD_SET_MAX_PAYLOAD(EPB, EPB_MAX_PKT_SIZE);
  HSUSBD_ConfigEp(EPB, BULK_OUT_EP_NUM, HSUSBD_EP_CFG_TYPE_BULK, HSUSBD_EP_CFG_DIR_OUT);
  HSUSBD_ENABLE_EP_INT(EPB, HSUSBD_EPINTEN_RXPKIEN_Msk);

  g_u32EpMaxPacketSize = EPA_MAX_PKT_SIZE;
}

void mscInitForFullSpeed(void)
{
  /* EPA ==> Bulk IN endpoint */
  HSUSBD_SetEpBufAddr(EPA, EPA_BUF_BASE, EPA_BUF_LEN);
  HSUSBD_SET_MAX_PAYLOAD(EPA, EPA_OTHER_MAX_PKT_SIZE);
  HSUSBD_ConfigEp(EPA, BULK_IN_EP_NUM, HSUSBD_EP_CFG_TYPE_BULK, HSUSBD_EP_CFG_DIR_IN);

  /* EPB ==> Bulk OUT endpoint */
  HSUSBD_SetEpBufAddr(EPB, EPB_BUF_BASE, EPB_BUF_LEN);
  HSUSBD_SET_MAX_PAYLOAD(EPB, EPB_OTHER_MAX_PKT_SIZE);
  HSUSBD_ConfigEp(EPB, BULK_OUT_EP_NUM, HSUSBD_EP_CFG_TYPE_BULK, HSUSBD_EP_CFG_DIR_OUT);
  HSUSBD_ENABLE_EP_INT(EPB, HSUSBD_EPINTEN_RXPKIEN_Msk);

  g_u32EpMaxPacketSize = EPA_OTHER_MAX_PKT_SIZE;
}

void mscInit(void)
{
  /* Enable USB BUS, CEP and EPA/EPB global interrupts */
  HSUSBD_ENABLE_USB_INT(HSUSBD_GINTEN_USBIEN_Msk | HSUSBD_GINTEN_CEPIEN_Msk |
                        HSUSBD_GINTEN_EPAIEN_Msk | HSUSBD_GINTEN_EPBIEN_Msk);
  HSUSBD_ENABLE_BUS_INT(HSUSBD_BUSINTEN_DMADONEIEN_Msk | HSUSBD_BUSINTEN_RESUMEIEN_Msk |
                        HSUSBD_BUSINTEN_RSTIEN_Msk | HSUSBD_BUSINTEN_VBUSDETIEN_Msk);
  HSUSBD_SET_ADDR(0);

  /* Control endpoint */
  HSUSBD_SetEpBufAddr(CEP, CEP_BUF_BASE, CEP_BUF_LEN);
  HSUSBD_ENABLE_CEP_INT(HSUSBD_CEPINTEN_SETUPPKIEN_Msk | HSUSBD_CEPINTEN_STSDONEIEN_Msk);

  /* High-speed by default (HSUSBD_Start sets HISPDEN). The bus-reset handler
   * re-inits EPs to the actual negotiated speed via OPER.CURSPD (mscOnBusReset). */
  mscInitForHighSpeed();

  g_sCSW.dCSWSignature = CSW_SIGNATURE;
  g_TotalSectors       = VDISK_TOTAL_SECTORS;
  g_u32MassBase        = (uint32_t)mscCmdBuf;

  g_u8BulkState    = BULK_CBW;
  g_u8MscOutPacket = 0;
}

void mscOnBusReset(void)
{
  g_u8BulkState    = BULK_CBW;
  g_u8MscOutPacket = 0;

  HSUSBD_ResetDMA();
  HSUSBD->EP[EPA].EPRSPCTL = HSUSBD_EPRSPCTL_FLUSH_Msk;
  HSUSBD->EP[EPB].EPRSPCTL = HSUSBD_EPRSPCTL_FLUSH_Msk;

  if (HSUSBD->OPER & 0x04)   /* high speed (not expected: FS device) */
    mscInitForHighSpeed();
  else
    mscInitForFullSpeed();
}

/*--------------------------------------------------------------------------*/
/* Class request (BOT reset / GET_MAX_LUN)                                   */
/*--------------------------------------------------------------------------*/
void mscClassRequest(void)
{
  if (gUsbCmd.bmRequestType & 0x80)   /* device-to-host */
  {
    switch (gUsbCmd.bRequest)
    {
      case GET_MAX_LUN:
        if ((gUsbCmd.wValue == 0) && (gUsbCmd.wIndex == 0) && (gUsbCmd.wLength == 1))
        {
          HSUSBD_PrepareCtrlIn((uint8_t *)&g_u32MSCMaxLun, 1);
          HSUSBD_CLR_CEP_INT_FLAG(HSUSBD_CEPINTSTS_INTKIF_Msk);
          HSUSBD_ENABLE_CEP_INT(HSUSBD_CEPINTEN_INTKIEN_Msk);
        }
        else
        {
          HSUSBD_SET_CEP_STATE(HSUSBD_CEPCTL_STALLEN_Msk);
        }
        break;
      default:
        HSUSBD_SET_CEP_STATE(HSUSBD_CEPCTL_STALLEN_Msk);
        break;
    }
  }
  else                                 /* host-to-device */
  {
    switch (gUsbCmd.bRequest)
    {
      case BULK_ONLY_MASS_STORAGE_RESET:
        if ((gUsbCmd.wValue == 0) && (gUsbCmd.wIndex == 0) && (gUsbCmd.wLength == 0))
        {
          g_u8Prevent = 1;
          /* Status stage */
          HSUSBD_CLR_CEP_INT_FLAG(HSUSBD_CEPINTSTS_STSDONEIF_Msk);
          HSUSBD_SET_CEP_STATE(HSUSBD_CEPCTL_NAKCLR);
          HSUSBD_ENABLE_CEP_INT(HSUSBD_CEPINTEN_STSDONEIEN_Msk);

          /* Reset the BOT pipe / state (BSP DataFlash behaviour). */
          g_u32HsEpStallLock = 0;
          HSUSBD_ResetDMA();
          HSUSBD->EP[EPA].EPRSPCTL = HSUSBD_EPRSPCTL_FLUSH_Msk;
          HSUSBD->EP[EPB].EPRSPCTL = HSUSBD_EPRSPCTL_FLUSH_Msk;
          g_u8BulkState    = BULK_CBW;
          g_u8MscOutPacket = 0;
        }
        else
        {
          HSUSBD_SET_CEP_STATE(HSUSBD_CEPCTL_STALLEN_Msk);
        }
        break;
      default:
        HSUSBD_SET_CEP_STATE(HSUSBD_CEPCTL_STALLEN_Msk);
        break;
    }
  }
}

/*--------------------------------------------------------------------------*/
/* SCSI response helpers (write into mscCmdBuf, then bulk-IN)                 */
/*--------------------------------------------------------------------------*/
static void MSC_RequestSense(void)
{
  memset((uint8_t *)(g_u32MassBase), 0, 18);
  if (g_u8Prevent)
  {
    g_u8Prevent = 0;
    *(uint8_t *)(g_u32MassBase) = 0x70;
  }
  else
    *(uint8_t *)(g_u32MassBase) = 0xf0;

  *(uint8_t *)(g_u32MassBase + 2)  = g_au8SenseKey[0];
  *(uint8_t *)(g_u32MassBase + 7)  = 0x0a;
  *(uint8_t *)(g_u32MassBase + 12) = g_au8SenseKey[1];
  *(uint8_t *)(g_u32MassBase + 13) = g_au8SenseKey[2];
  MSC_BulkIn(g_u32MassBase, g_sCBW.dCBWDataTransferLength);

  g_au8SenseKey[0] = 0;
  g_au8SenseKey[1] = 0;
  g_au8SenseKey[2] = 0;
}

static void MSC_ReadFormatCapacity(void)
{
  memset((uint8_t *)g_u32MassBase, 0, 36);

  *((uint8_t *)(g_u32MassBase + 3))  = 0x10;
  *((uint8_t *)(g_u32MassBase + 4))  = *((uint8_t *)&g_TotalSectors + 3);
  *((uint8_t *)(g_u32MassBase + 5))  = *((uint8_t *)&g_TotalSectors + 2);
  *((uint8_t *)(g_u32MassBase + 6))  = *((uint8_t *)&g_TotalSectors + 1);
  *((uint8_t *)(g_u32MassBase + 7))  = *((uint8_t *)&g_TotalSectors + 0);
  *((uint8_t *)(g_u32MassBase + 8))  = 0x02;
  *((uint8_t *)(g_u32MassBase + 10)) = 0x02;
  *((uint8_t *)(g_u32MassBase + 12)) = *((uint8_t *)&g_TotalSectors + 3);
  *((uint8_t *)(g_u32MassBase + 13)) = *((uint8_t *)&g_TotalSectors + 2);
  *((uint8_t *)(g_u32MassBase + 14)) = *((uint8_t *)&g_TotalSectors + 1);
  *((uint8_t *)(g_u32MassBase + 15)) = *((uint8_t *)&g_TotalSectors + 0);
  *((uint8_t *)(g_u32MassBase + 18)) = 0x02;

  MSC_BulkIn(g_u32MassBase, g_sCBW.dCBWDataTransferLength);
}

static void MSC_ReadCapacity(void)
{
  uint32_t tmp;

  memset((uint8_t *)g_u32MassBase, 0, 36);

  tmp = g_TotalSectors - 1;
  *((uint8_t *)(g_u32MassBase + 0)) = *((uint8_t *)&tmp + 3);
  *((uint8_t *)(g_u32MassBase + 1)) = *((uint8_t *)&tmp + 2);
  *((uint8_t *)(g_u32MassBase + 2)) = *((uint8_t *)&tmp + 1);
  *((uint8_t *)(g_u32MassBase + 3)) = *((uint8_t *)&tmp + 0);
  *((uint8_t *)(g_u32MassBase + 6)) = 0x02;   /* block size = 512 */

  MSC_BulkIn(g_u32MassBase, g_sCBW.dCBWDataTransferLength);
}

static void MSC_ModeSense10(void)
{
  uint8_t  i, j;
  uint8_t  NumHead, NumSector;
  uint16_t NumCyl = 0;

  *((uint32_t *)g_u32MassBase)       = 0;
  *((uint32_t *)g_u32MassBase + 1)   = 0;

  switch (g_sCBW.au8Data[0])
  {
    case 0x01:
      *((uint8_t *)g_u32MassBase) = 19;
      i = 8;
      for (j = 0; j < 12; j++, i++)
        *((uint8_t *)(g_u32MassBase + i)) = g_au8ModePage_01[j];
      break;

    case 0x05:
      *((uint8_t *)g_u32MassBase) = 39;
      i = 8;
      for (j = 0; j < 32; j++, i++)
        *((uint8_t *)(g_u32MassBase + i)) = g_au8ModePage_05[j];

      NumHead   = 2;
      NumSector = 64;
      NumCyl    = g_TotalSectors / 128;

      *((uint8_t *)(g_u32MassBase + 12)) = NumHead;
      *((uint8_t *)(g_u32MassBase + 13)) = NumSector;
      *((uint8_t *)(g_u32MassBase + 16)) = (uint8_t)(NumCyl >> 8);
      *((uint8_t *)(g_u32MassBase + 17)) = (uint8_t)(NumCyl & 0x00ff);
      break;

    case 0x1B:
      *((uint8_t *)g_u32MassBase) = 19;
      i = 8;
      for (j = 0; j < 12; j++, i++)
        *((uint8_t *)(g_u32MassBase + i)) = g_au8ModePage_1B[j];
      break;

    case 0x1C:
      *((uint8_t *)g_u32MassBase) = 15;
      i = 8;
      for (j = 0; j < 8; j++, i++)
        *((uint8_t *)(g_u32MassBase + i)) = g_au8ModePage_1C[j];
      break;

    case 0x3F:
      *((uint8_t *)g_u32MassBase) = 0x47;
      i = 8;
      for (j = 0; j < 12; j++, i++)
        *((uint8_t *)(g_u32MassBase + i)) = g_au8ModePage_01[j];
      for (j = 0; j < 32; j++, i++)
        *((uint8_t *)(g_u32MassBase + i)) = g_au8ModePage_05[j];
      for (j = 0; j < 12; j++, i++)
        *((uint8_t *)(g_u32MassBase + i)) = g_au8ModePage_1B[j];
      for (j = 0; j < 8; j++, i++)
        *((uint8_t *)(g_u32MassBase + i)) = g_au8ModePage_1C[j];

      NumHead   = 2;
      NumSector = 64;
      NumCyl    = g_TotalSectors / 128;

      *((uint8_t *)(g_u32MassBase + 24)) = NumHead;
      *((uint8_t *)(g_u32MassBase + 25)) = NumSector;
      *((uint8_t *)(g_u32MassBase + 28)) = (uint8_t)(NumCyl >> 8);
      *((uint8_t *)(g_u32MassBase + 29)) = (uint8_t)(NumCyl & 0x00ff);
      break;

    default:
      g_au8SenseKey[0] = 0x05;
      g_au8SenseKey[1] = 0x24;
      g_au8SenseKey[2] = 0x00;
  }
  MSC_BulkIn(g_u32MassBase, g_sCBW.dCBWDataTransferLength);
}

static void MSC_ModeSense6(void)
{
  uint8_t i;

  for (i = 0; i < 4; i++)
    *((uint8_t *)(g_u32MassBase + i)) = g_au8ModePage[i];

  MSC_BulkIn(g_u32MassBase, g_sCBW.dCBWDataTransferLength);
}

/*--------------------------------------------------------------------------*/
/* Low-level EP-DMA transfers (busy-wait on DMA-done, main-loop context)     */
/*--------------------------------------------------------------------------*/
static void MSC_ActiveDMA(uint32_t u32Addr, uint32_t u32Len)
{
  HSUSBD_ENABLE_BUS_INT(HSUSBD_BUSINTEN_DMADONEIEN_Msk | HSUSBD_BUSINTEN_SUSPENDIEN_Msk |
                        HSUSBD_BUSINTEN_RSTIEN_Msk | HSUSBD_BUSINTEN_VBUSDETIEN_Msk);

  HSUSBD_SET_DMA_ADDR(u32Addr);
  HSUSBD_SET_DMA_LEN(u32Len);
  g_hsusbd_DmaDone = 0;
  HSUSBD_ENABLE_DMA();

  while (g_hsusbd_Configured)
  {
    if (g_hsusbd_DmaDone)
      break;
    if (!HSUSBD_IS_ATTACHED())
      break;
  }
}

static void MSC_BulkIn(uint32_t u32Addr, uint32_t u32Len)
{
  uint32_t u32Loop;
  uint32_t i, addr, count;

  /* bulk in, dma read */
  HSUSBD_SET_DMA_READ(BULK_IN_EP_NUM);

  u32Loop = u32Len / USBD_MAX_DMA_LEN;
  for (i = 0; i < u32Loop; i++)
  {
    HSUSBD_ENABLE_EP_INT(EPA, HSUSBD_EPINTEN_TXPKIEN_Msk);
    g_hsusbd_ShortPacket = 0;
    while (1)
    {
      if (HSUSBD_GET_EP_INT_FLAG(EPA) & HSUSBD_EPINTSTS_BUFEMPTYIF_Msk)
      {
        MSC_ActiveDMA(u32Addr + i * USBD_MAX_DMA_LEN, USBD_MAX_DMA_LEN);
        break;
      }
    }
  }

  addr    = u32Addr + i * USBD_MAX_DMA_LEN;
  u32Loop = u32Len % USBD_MAX_DMA_LEN;
  if (u32Loop)
  {
    count = u32Loop / g_u32EpMaxPacketSize;
    if (count)
    {
      HSUSBD_ENABLE_EP_INT(EPA, HSUSBD_EPINTEN_TXPKIEN_Msk);
      g_hsusbd_ShortPacket = 0;
      while (1)
      {
        if (HSUSBD_GET_EP_INT_FLAG(EPA) & HSUSBD_EPINTSTS_BUFEMPTYIF_Msk)
        {
          MSC_ActiveDMA(addr, count * g_u32EpMaxPacketSize);
          break;
        }
      }
      addr += (count * g_u32EpMaxPacketSize);
    }
    count = u32Loop % g_u32EpMaxPacketSize;
    if (count)
    {
      HSUSBD_ENABLE_EP_INT(EPA, HSUSBD_EPINTEN_TXPKIEN_Msk);
      g_hsusbd_ShortPacket = 1;
      while (1)
      {
        if (HSUSBD_GET_EP_INT_FLAG(EPA) & HSUSBD_EPINTSTS_BUFEMPTYIF_Msk)
        {
          MSC_ActiveDMA(addr, count);
          break;
        }
      }
    }
  }
}

/* Receive exactly one 512-byte sector from the bulk-OUT endpoint by DMA. */
static void MSC_BulkOutSector(uint32_t u32Addr)
{
  HSUSBD_SET_DMA_WRITE(BULK_OUT_EP_NUM);
  g_hsusbd_ShortPacket = 0;
  MSC_ActiveDMA(u32Addr, USBD_SECTOR_SIZE);   /* 512 < USBD_MAX_DMA_LEN */
}

/* DMA the bytes already sitting in the bulk-OUT FIFO (length from EPDATCNT).
 * Only called after g_u8MscOutPacket signalled that an OUT packet arrived. */
static void MSC_ReceiveCBW(uint32_t u32Buf, uint32_t u32Len)
{
  HSUSBD_SET_DMA_WRITE(BULK_OUT_EP_NUM);

  HSUSBD_ENABLE_BUS_INT(HSUSBD_BUSINTEN_DMADONEIEN_Msk | HSUSBD_BUSINTEN_SUSPENDIEN_Msk |
                        HSUSBD_BUSINTEN_RSTIEN_Msk | HSUSBD_BUSINTEN_VBUSDETIEN_Msk);

  HSUSBD_SET_DMA_ADDR(u32Buf);
  HSUSBD_SET_DMA_LEN(u32Len);

  g_hsusbd_DmaDone = 0;
  HSUSBD_ENABLE_DMA();

  while (g_hsusbd_Configured)
  {
    if (g_hsusbd_DmaDone == 1)
      break;
    if (!HSUSBD_IS_ATTACHED())
      break;
  }
}

/* Send the 13-byte CSW (fields must be filled in by the caller). */
static void MSC_AckCmd(void)
{
  HSUSBD_MemCopy((uint8_t *)g_u32MassBase, (uint8_t *)&g_sCSW.dCSWSignature, 13);
  MSC_BulkIn(g_u32MassBase, 13);
  g_u8BulkState    = BULK_CBW;
  g_u8MscOutPacket = 0;
}

/*--------------------------------------------------------------------------*/
/* READ10 : serve sectors from the synthesized FAT12 image                   */
/*--------------------------------------------------------------------------*/
static void MSC_Read10(void)
{
  uint32_t lba  = get_be32(&g_sCBW.au8Data[0]);
  uint32_t nsec = g_sCBW.dCBWDataTransferLength / USBD_SECTOR_SIZE;
  uint32_t rem  = g_sCBW.dCBWDataTransferLength % USBD_SECTOR_SIZE;

  for (uint32_t i = 0; i < nsec; i++)
  {
    vdiskRead(lba + i, mscReadBuf);
    MSC_BulkIn((uint32_t)mscReadBuf, USBD_SECTOR_SIZE);
  }
  if (rem)   /* short trailing request (unusual): serve partial sector */
  {
    vdiskRead(lba + nsec, mscReadBuf);
    MSC_BulkIn((uint32_t)mscReadBuf, rem);
  }
}

/*--------------------------------------------------------------------------*/
/* WRITE10 : receive the whole data phase inline, one sector at a time, and  */
/* hand each sector to the registered write callback before the CSW.         */
/*--------------------------------------------------------------------------*/
static void MSC_Write10(uint32_t Hcount)
{
  uint32_t lba  = get_be32(&g_sCBW.au8Data[0]);
  uint32_t nsec = Hcount / USBD_SECTOR_SIZE;

  g_u32LbaAddress = lba;

#if MSC_TRACE_CBW
  logPrintf("WR10 lba=%u n=%u\r\n", (unsigned)lba, (unsigned)nsec);
#endif

  for (uint32_t i = 0; i < nsec; i++)
  {
    MSC_BulkOutSector((uint32_t)mscWriteBuf);   /* DMA one 512B OUT sector */
    if (g_msc_write_cb != NULL)
      (void)g_msc_write_cb(lba + i, mscWriteBuf);
  }

#if MSC_TRACE_CBW
  logPrintf("WR10 done\r\n");
#endif
}

/*--------------------------------------------------------------------------*/
/* Polled BOT/SCSI worker (main loop)                                        */
/*--------------------------------------------------------------------------*/
void mscProcess(void)
{
  uint32_t i;
  uint32_t Hcount;

  if (!g_hsusbd_Configured)
    return;

  /* ---------------------------------------------------------------------
   * CBW reception : EVENT-DRIVEN. Only act once the EPB RXPKIF ISR has
   * signalled that an OUT packet (the CBW) actually arrived.
   * ------------------------------------------------------------------- */
  if (!g_u8MscOutPacket)
    return;
  g_u8MscOutPacket = 0;

  if (g_u8BulkState != BULK_CBW)
    return;

  /* DMA the bytes waiting in the bulk-OUT FIFO (actual length from EPDATCNT). */
  g_u32CbwSize = HSUSBD->EP[EPB].EPDATCNT & 0xffff;
  MSC_ReceiveCBW(g_u32MassBase, g_u32CbwSize);

  /* Validate CBW signature AND length; stall both bulk EPs on a bad CBW. */
  if ((*(uint32_t *)(g_u32MassBase) != CBW_SIGNATURE) || (g_u32CbwSize != 31))
  {
    g_u8Prevent = 1;
    HSUSBD_SetEpStall(EPA);
    HSUSBD_SetEpStall(EPB);
    g_u32HsEpStallLock = (1 << EPA) | (1 << EPB);
    return;
  }

  /* Copy CBW into the structured view. */
  for (i = 0; i < 31; i++)
    *((uint8_t *)(&g_sCBW.dCBWSignature) + i) = *(uint8_t *)(g_u32MassBase + i);

  g_sCSW.dCSWTag = g_sCBW.dCBWTag;
  Hcount         = g_sCBW.dCBWDataTransferLength;

#if MSC_TRACE_CBW
  logPrintf("CBW op=0x%02X len=%u\r\n", g_sCBW.u8OPCode, (unsigned)Hcount);
#endif

  switch (g_sCBW.u8OPCode)
  {
    case UFI_READ_10:
      MSC_Read10();
      g_sCSW.dCSWDataResidue = 0;
      g_sCSW.bCSWStatus      = 0;
      MSC_AckCmd();
      break;

    case UFI_WRITE_10:
      /* Receive the ENTIRE data phase inline (one mscProcess() call), then
       * ACK. mscProcess() runs in the main loop, so the write callback
       * (uf2_write_block via ap.c) is off the IRQ path. */
      MSC_Write10(Hcount);
      g_sCSW.dCSWDataResidue = 0;
      g_sCSW.bCSWStatus      = 0;
      MSC_AckCmd();
      break;

    case UFI_PREVENT_ALLOW_MEDIUM_REMOVAL:
      if (g_sCBW.au8Data[2] & 0x01)
      {
        g_au8SenseKey[0] = 0x05;
        g_au8SenseKey[1] = 0x24;
        g_au8SenseKey[2] = 0;
        g_u8Prevent      = 1;
      }
      else
        g_u8Prevent = 0;
      g_sCSW.dCSWDataResidue = 0;
      g_sCSW.bCSWStatus      = g_u8Prevent;
      MSC_AckCmd();
      break;

    case UFI_START_STOP:
      /* START STOP UNIT (CDB byte4 = au8Data[2]) : bit1=LOEJ, bit0=START.
       * 호스트가 장치 사용을 마치면 STOP(START=0)을 보낸다 -> 부트 종료(앱 점프) 신호:
       *   - macOS 꺼내기      : byte4=0x00 (LOEJ=0, START=0)
       *   - Windows 안전제거   : byte4=0x02 (LOEJ=1, START=0)
       * 마운트 시의 LOAD(START=1)는 무시. 래치만 걸고 정상 ACK -> CSW 전송 후
       * 메인루프가 bootJumpFirm(). */
      logPrintf("SCSI START_STOP: byte4=0x%02X (LOEJ=%d START=%d)\r\n",
                g_sCBW.au8Data[2],
                (g_sCBW.au8Data[2] >> 1) & 1, g_sCBW.au8Data[2] & 1);
      if (!(g_sCBW.au8Data[2] & 0x01))   /* START=0 : stop/eject */
      {
        g_msc_eject = 1;
        logPrintf("SCSI START_STOP: stop/eject latched\r\n");
      }
      g_sCSW.dCSWDataResidue = 0;
      g_sCSW.bCSWStatus      = 0;
      MSC_AckCmd();
      break;

    case UFI_VERIFY_10:
    case UFI_TEST_UNIT_READY:
      g_sCSW.dCSWDataResidue = 0;
      g_sCSW.bCSWStatus      = 0;
      MSC_AckCmd();
      break;

    case UFI_REQUEST_SENSE:
      MSC_RequestSense();
      g_sCSW.dCSWDataResidue = 0;
      g_sCSW.bCSWStatus      = 0;
      MSC_AckCmd();
      break;

    case UFI_READ_FORMAT_CAPACITY:
      MSC_ReadFormatCapacity();
      g_sCSW.dCSWDataResidue = 0;
      g_sCSW.bCSWStatus      = 0;
      MSC_AckCmd();
      break;

    case UFI_READ_CAPACITY:
      MSC_ReadCapacity();
      g_sCSW.dCSWDataResidue = 0;
      g_sCSW.bCSWStatus      = 0;
      MSC_AckCmd();
      break;

    case UFI_MODE_SELECT_10:
      /* Receive and discard the parameter list. */
      MSC_BulkOutSector(g_u32MassBase);
      g_sCSW.dCSWDataResidue = 0;
      g_sCSW.bCSWStatus      = 0;
      MSC_AckCmd();
      break;

    case UFI_MODE_SENSE_10:
      MSC_ModeSense10();
      g_sCSW.dCSWDataResidue = 0;
      g_sCSW.bCSWStatus      = 0;
      MSC_AckCmd();
      break;

    case UFI_MODE_SENSE_6:
      MSC_ModeSense6();
      g_sCSW.dCSWDataResidue = 0;
      g_sCSW.bCSWStatus      = 0;
      MSC_AckCmd();
      break;

    case UFI_INQUIRY:
      if ((Hcount > 0) && (Hcount <= 36))
      {
        HSUSBD_MemCopy((uint8_t *)(g_u32MassBase), (uint8_t *)g_au8InquiryID, Hcount);
        MSC_BulkIn(g_u32MassBase, Hcount);
        g_sCSW.dCSWDataResidue = 0;
        g_sCSW.bCSWStatus      = 0;
      }
      else
      {
        HSUSBD_SetEpStall(EPA);
        g_u8Prevent            = 1;
        g_sCSW.bCSWStatus      = 0x01;
        g_sCSW.dCSWDataResidue = 0;
      }
      MSC_AckCmd();
      break;

    default:
      /* Unsupported command */
      g_au8SenseKey[0] = 0x05;
      g_au8SenseKey[1] = 0x20;
      g_au8SenseKey[2] = 0x00;
      g_sCSW.dCSWDataResidue = Hcount;
      g_sCSW.bCSWStatus      = g_u8Prevent;
      MSC_AckCmd();
      break;
  }
}
