/*
 * usbd_conf.c
 *
 *  HSUSBD low-level backend for the full-speed MSC bootloader.
 *
 *  PHY / clock bring-up ported verbatim from m483-fw usbd_conf.c (usbPhyInit).
 *  Device is brought up at FULL-SPEED: HSUSBD_Open() leaves OPER.HISPDEN clear
 *  (FS default) and we attach with HSUSBD_CLR_SE0() instead of HSUSBD_Start()
 *  (which would set HISPDEN for high-speed).
 *
 *  USBD20_IRQHandler ported from the BSP HSUSBD MSC sample: it services the
 *  control pipe, bus events and DMA-done signalling. The BOT/SCSI command work
 *  is polled from the main loop (mscProcess), so EPA/EPB IRQs only clear flags.
 */
#include "usbd_conf.h"

#include "NuMicro.h"
#include "usb_msc/usbd_msc.h"

/* gsHSInfo is defined in usbd_desc.c and declared extern by hsusbd.h. */

/*--------------------------------------------------------------------------*/
/* PHY / clock                                                              */
/*--------------------------------------------------------------------------*/
static void usbPhyInit(void)
{
  volatile int32_t i;

  SYS_UnlockReg();

  /* HSUSB PHY : device role, enable, activate (order matters, BSP port) */
  SYS->USBPHY = (SYS->USBPHY & ~(SYS_USBPHY_HSUSBROLE_Msk | SYS_USBPHY_HSUSBACT_Msk)) |
                SYS_USBPHY_HSUSBEN_Msk;

  /* > 10us delay before activating the PHY */
  for (i = 0; i < 0x1000; i++);

  SYS->USBPHY |= SYS_USBPHY_HSUSBACT_Msk;

  /* HSUSBD peripheral clock */
  CLK_EnableModuleClock(HSUSBD_MODULE);

  SYS_LockReg();
}

void usbdConfInit(void)
{
  usbPhyInit();

  /* HSUSBD_Open clears OPER.HISPDEN -> full-speed default. */
  HSUSBD_Open(&gsHSInfo, mscClassRequest, NULL);

  /* MSC endpoint configuration + BOT/SCSI state. */
  mscInit();

  /* Enable HSUSBD interrupt. */
  NVIC_EnableIRQ(USBD20_IRQn);

  /* HIGH-SPEED attach (HSUSBD_Start sets HISPDEN then CLR_SE0). HSUSBD is a
   * native USB 2.0 HS PHY; running it in hand-forced full-speed (HISPDEN clear)
   * is non-standard and enumerates unreliably on some hosts (macOS). The BSP
   * MSC samples and the m483-fw app both attach with HSUSBD_Start(). */
  HSUSBD_Start();
}

bool usbdConfIsConfigured(void)
{
  return (g_hsusbd_Configured != 0);
}

void usbdConfDisconnect(void)
{
  /* 재부착을 막기 위해 IRQ부터 차단(VBUSDET 핸들러가 HSUSBD_ENABLE_USB로 풀업을
   * 다시 켜는 것 방지), 그다음 SE0로 D+ 풀업 드롭 -> 호스트가 제거로 인식. */
  NVIC_DisableIRQ(USBD20_IRQn);
  HSUSBD_SET_SE0();
}

/*--------------------------------------------------------------------------*/
/* HSUSBD interrupt                                                          */
/*--------------------------------------------------------------------------*/
void USBD20_IRQHandler(void)
{
  __IO uint32_t IrqStL, IrqSt;

  IrqStL = HSUSBD->GINTSTS & HSUSBD->GINTEN;    /* get interrupt status */

  if (!IrqStL)
    return;

  /* USB interrupt */
  if (IrqStL & HSUSBD_GINTSTS_USBIF_Msk)
  {
    IrqSt = HSUSBD->BUSINTSTS & HSUSBD->BUSINTEN;

    if (IrqSt & HSUSBD_BUSINTSTS_SOFIF_Msk)
      HSUSBD_CLR_BUS_INT_FLAG(HSUSBD_BUSINTSTS_SOFIF_Msk);

    if (IrqSt & HSUSBD_BUSINTSTS_RSTIF_Msk)
    {
      HSUSBD_SwReset();
      mscOnBusReset();

      HSUSBD_ENABLE_CEP_INT(HSUSBD_CEPINTEN_SETUPPKIEN_Msk);
      HSUSBD_SET_ADDR(0);
      HSUSBD_ENABLE_BUS_INT(HSUSBD_BUSINTEN_RSTIEN_Msk | HSUSBD_BUSINTEN_RESUMEIEN_Msk |
                            HSUSBD_BUSINTEN_SUSPENDIEN_Msk);
      HSUSBD_CLR_BUS_INT_FLAG(HSUSBD_BUSINTSTS_RSTIF_Msk);
      HSUSBD_CLR_CEP_INT_FLAG(0x1ffc);
    }

    if (IrqSt & HSUSBD_BUSINTSTS_RESUMEIF_Msk)
    {
      HSUSBD_ENABLE_BUS_INT(HSUSBD_BUSINTEN_RSTIEN_Msk | HSUSBD_BUSINTEN_SUSPENDIEN_Msk);
      HSUSBD_CLR_BUS_INT_FLAG(HSUSBD_BUSINTSTS_RESUMEIF_Msk);
    }

    if (IrqSt & HSUSBD_BUSINTSTS_SUSPENDIF_Msk)
    {
      HSUSBD_ENABLE_BUS_INT(HSUSBD_BUSINTEN_RSTIEN_Msk | HSUSBD_BUSINTEN_RESUMEIEN_Msk);
      HSUSBD_CLR_BUS_INT_FLAG(HSUSBD_BUSINTSTS_SUSPENDIF_Msk);
    }

    if (IrqSt & HSUSBD_BUSINTSTS_HISPDIF_Msk)
    {
      HSUSBD_ENABLE_CEP_INT(HSUSBD_CEPINTEN_SETUPPKIEN_Msk);
      HSUSBD_CLR_BUS_INT_FLAG(HSUSBD_BUSINTSTS_HISPDIF_Msk);
    }

    if (IrqSt & HSUSBD_BUSINTSTS_DMADONEIF_Msk)
    {
      g_hsusbd_DmaDone = 1;
      HSUSBD_CLR_BUS_INT_FLAG(HSUSBD_BUSINTSTS_DMADONEIF_Msk);

      /* EPB (bulk-OUT) RXPKIEN stays enabled for the whole session (matches the
       * BSP), so nothing to re-arm here on OUT-DMA completion. */

      if (HSUSBD->DMACTL & HSUSBD_DMACTL_DMARD_Msk)
      {
        if (g_hsusbd_ShortPacket == 1)
        {
          HSUSBD->EP[EPA].EPRSPCTL = (HSUSBD->EP[EPA].EPRSPCTL & 0x10) | HSUSBD_EP_RSPCTL_SHORTTXEN;
          g_hsusbd_ShortPacket = 0;
        }
      }
    }

    if (IrqSt & HSUSBD_BUSINTSTS_PHYCLKVLDIF_Msk)
      HSUSBD_CLR_BUS_INT_FLAG(HSUSBD_BUSINTSTS_PHYCLKVLDIF_Msk);

    if (IrqSt & HSUSBD_BUSINTSTS_VBUSDETIF_Msk)
    {
      if (HSUSBD_IS_ATTACHED())
        HSUSBD_ENABLE_USB();
      else
        HSUSBD_DISABLE_USB();

      HSUSBD_CLR_BUS_INT_FLAG(HSUSBD_BUSINTSTS_VBUSDETIF_Msk);
    }
  }

  /* Control endpoint */
  if (IrqStL & HSUSBD_GINTSTS_CEPIF_Msk)
  {
    IrqSt = HSUSBD->CEPINTSTS & HSUSBD->CEPINTEN;

    if (IrqSt & HSUSBD_CEPINTSTS_SETUPTKIF_Msk)
    {
      HSUSBD_CLR_CEP_INT_FLAG(HSUSBD_CEPINTSTS_SETUPTKIF_Msk);
      return;
    }

    if (IrqSt & HSUSBD_CEPINTSTS_SETUPPKIF_Msk)
    {
      HSUSBD_CLR_CEP_INT_FLAG(HSUSBD_CEPINTSTS_SETUPPKIF_Msk);
      HSUSBD_ProcessSetupPacket();
      return;
    }

    if (IrqSt & HSUSBD_CEPINTSTS_OUTTKIF_Msk)
    {
      HSUSBD_CLR_CEP_INT_FLAG(HSUSBD_CEPINTSTS_OUTTKIF_Msk);
      HSUSBD_ENABLE_CEP_INT(HSUSBD_CEPINTEN_STSDONEIEN_Msk);
      return;
    }

    if (IrqSt & HSUSBD_CEPINTSTS_INTKIF_Msk)
    {
      HSUSBD_CLR_CEP_INT_FLAG(HSUSBD_CEPINTSTS_INTKIF_Msk);
      if (!(IrqSt & HSUSBD_CEPINTSTS_STSDONEIF_Msk))
      {
        HSUSBD_CLR_CEP_INT_FLAG(HSUSBD_CEPINTSTS_TXPKIF_Msk);
        HSUSBD_ENABLE_CEP_INT(HSUSBD_CEPINTEN_TXPKIEN_Msk);
        HSUSBD_CtrlIn();
      }
      else
      {
        HSUSBD_CLR_CEP_INT_FLAG(HSUSBD_CEPINTSTS_TXPKIF_Msk);
        HSUSBD_ENABLE_CEP_INT(HSUSBD_CEPINTEN_TXPKIEN_Msk | HSUSBD_CEPINTEN_STSDONEIEN_Msk);
      }
      return;
    }

    if (IrqSt & HSUSBD_CEPINTSTS_PINGIF_Msk)
    {
      HSUSBD_CLR_CEP_INT_FLAG(HSUSBD_CEPINTSTS_PINGIF_Msk);
      return;
    }

    if (IrqSt & HSUSBD_CEPINTSTS_TXPKIF_Msk)
    {
      HSUSBD_CLR_CEP_INT_FLAG(HSUSBD_CEPINTSTS_STSDONEIF_Msk);
      HSUSBD_SET_CEP_STATE(HSUSBD_CEPCTL_NAKCLR);
      if (g_hsusbd_CtrlInSize)
      {
        HSUSBD_CLR_CEP_INT_FLAG(HSUSBD_CEPINTSTS_INTKIF_Msk);
        HSUSBD_ENABLE_CEP_INT(HSUSBD_CEPINTEN_INTKIEN_Msk);
      }
      else
      {
        if (g_hsusbd_CtrlZero == 1)
          HSUSBD_SET_CEP_STATE(HSUSBD_CEPCTL_ZEROLEN);
        HSUSBD_CLR_CEP_INT_FLAG(HSUSBD_CEPINTSTS_STSDONEIF_Msk);
        HSUSBD_ENABLE_CEP_INT(HSUSBD_CEPINTEN_SETUPPKIEN_Msk | HSUSBD_CEPINTEN_STSDONEIEN_Msk);
      }
      HSUSBD_CLR_CEP_INT_FLAG(HSUSBD_CEPINTSTS_TXPKIF_Msk);
      return;
    }

    if (IrqSt & HSUSBD_CEPINTSTS_RXPKIF_Msk)
    {
      HSUSBD_CLR_CEP_INT_FLAG(HSUSBD_CEPINTSTS_RXPKIF_Msk);
      HSUSBD_SET_CEP_STATE(HSUSBD_CEPCTL_NAKCLR);
      HSUSBD_ENABLE_CEP_INT(HSUSBD_CEPINTEN_SETUPPKIEN_Msk | HSUSBD_CEPINTEN_STSDONEIEN_Msk);
      return;
    }

    if (IrqSt & HSUSBD_CEPINTSTS_NAKIF_Msk)
    {
      HSUSBD_CLR_CEP_INT_FLAG(HSUSBD_CEPINTSTS_NAKIF_Msk);
      return;
    }

    if (IrqSt & HSUSBD_CEPINTSTS_STALLIF_Msk)
    {
      HSUSBD_CLR_CEP_INT_FLAG(HSUSBD_CEPINTSTS_STALLIF_Msk);
      return;
    }

    if (IrqSt & HSUSBD_CEPINTSTS_ERRIF_Msk)
    {
      HSUSBD_CLR_CEP_INT_FLAG(HSUSBD_CEPINTSTS_ERRIF_Msk);
      return;
    }

    if (IrqSt & HSUSBD_CEPINTSTS_STSDONEIF_Msk)
    {
      HSUSBD_UpdateDeviceState();
      HSUSBD_CLR_CEP_INT_FLAG(HSUSBD_CEPINTSTS_STSDONEIF_Msk);
      HSUSBD_ENABLE_CEP_INT(HSUSBD_CEPINTEN_SETUPPKIEN_Msk);
      return;
    }

    if (IrqSt & HSUSBD_CEPINTSTS_BUFFULLIF_Msk)
    {
      HSUSBD_CLR_CEP_INT_FLAG(HSUSBD_CEPINTSTS_BUFFULLIF_Msk);
      return;
    }

    if (IrqSt & HSUSBD_CEPINTSTS_BUFEMPTYIF_Msk)
    {
      HSUSBD_CLR_CEP_INT_FLAG(HSUSBD_CEPINTSTS_BUFEMPTYIF_Msk);
      return;
    }
  }

  /* bulk-IN (EPA) : transfers are DMA/busy-wait driven, just clear flags */
  if (IrqStL & HSUSBD_GINTSTS_EPAIF_Msk)
  {
    IrqSt = HSUSBD->EP[EPA].EPINTSTS & HSUSBD->EP[EPA].EPINTEN;
    HSUSBD_ENABLE_EP_INT(EPA, 0);
    HSUSBD_CLR_EP_INT_FLAG(EPA, IrqSt);
  }

  /* bulk-OUT (EPB) : signal that an OUT packet (CBW) arrived. Matches the BSP
   * DataFlash handler - the RXPKIEN interrupt is left enabled throughout; the
   * polled mscProcess() consumes g_u8MscOutPacket for CBW reception and drains
   * the WRITE10 data phase inline. */
  if (IrqStL & HSUSBD_GINTSTS_EPBIF_Msk)
  {
    IrqSt = HSUSBD->EP[EPB].EPINTSTS & HSUSBD->EP[EPB].EPINTEN;
    if (IrqSt & HSUSBD_EPINTSTS_RXPKIF_Msk)
      g_u8MscOutPacket = 1;
    HSUSBD_CLR_EP_INT_FLAG(EPB, IrqSt);
  }
}
