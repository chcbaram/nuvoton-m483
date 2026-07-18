/*
 * usbd_conf.c
 *
 *  HSUSBD 저수준 백엔드.
 *  - HSUSB PHY/클럭 bring-up (bsp.c 의 HXT+PLL192 위에 PHY 만 추가)
 *  - HSUSBD_Open / EP 구성 / IRQ enable / HSUSBD_Start
 *  - USBD20_IRQHandler : BUS/CEP/EPA 인터럽트 처리, SOF/reset/suspend 를
 *    usb_hid 측정 훅으로 전달
 *  Nuvoton BSP HSUSBD_HID_MouseKeyboard (main.c SYS_Init, hid_*.c IRQ) 이식.
 */

#include "usbd_conf.h"

#ifdef _USE_HW_USB

#include "usbd_desc.h"
#include "usb_hid/usbd_hid.h"


static void usbPhyInit(void)
{
  volatile int32_t i;

  SYS_UnlockReg();

  /* HSUSB PHY : device role, enable, activate (순서 중요, BSP 이식) */
  SYS->USBPHY = (SYS->USBPHY & ~(SYS_USBPHY_HSUSBROLE_Msk | SYS_USBPHY_HSUSBACT_Msk)) |
                SYS_USBPHY_HSUSBEN_Msk;

  /* > 10us 지연 후 PHY activate */
  for (i = 0; i < 0x1000; i++);

  SYS->USBPHY |= SYS_USBPHY_HSUSBACT_Msk;

  /* HSUSBD peripheral clock */
  CLK_EnableModuleClock(HSUSBD_MODULE);

  SYS_LockReg();
}

void usbdConfInit(void)
{
  usbPhyInit();

  HSUSBD_Open(&gsHSInfo, HID_ClassRequest, NULL);
  HSUSBD_SetVendorRequest(HID_VendorRequest);

  /* Endpoint 구성 */
  usbHidInit();

  /* Enable HSUSBD interrupt */
  NVIC_EnableIRQ(USBD20_IRQn);

  /* HISPDEN 세트 + 버스 attach : High-Speed 로 열거 시작 */
  HSUSBD_Start();
}

bool usbdConfIsConfigured(void)
{
  return (g_hsusbd_Configured != 0);
}


/*--------------------------------------------------------------------------*/
/* HSUSBD interrupt                                                         */
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
    {
      usbHidOnSof();
      HSUSBD_CLR_BUS_INT_FLAG(HSUSBD_BUSINTSTS_SOFIF_Msk);
    }

    if (IrqSt & HSUSBD_BUSINTSTS_RSTIF_Msk)
    {
      HSUSBD_SwReset();
      usbHidOnBusReset();

      HSUSBD_ENABLE_CEP_INT(HSUSBD_CEPINTEN_SETUPPKIEN_Msk);
      HSUSBD_SET_ADDR(0);
      HSUSBD_ENABLE_BUS_INT(HSUSBD_BUSINTEN_RSTIEN_Msk |
                            HSUSBD_BUSINTEN_RESUMEIEN_Msk |
                            HSUSBD_BUSINTEN_SUSPENDIEN_Msk |
                            HSUSBD_BUSINTEN_SOFIEN_Msk);
      HSUSBD_CLR_BUS_INT_FLAG(HSUSBD_BUSINTSTS_RSTIF_Msk);
      HSUSBD_CLR_CEP_INT_FLAG(0x1ffc);
    }

    if (IrqSt & HSUSBD_BUSINTSTS_RESUMEIF_Msk)
    {
      HSUSBD_ENABLE_BUS_INT(HSUSBD_BUSINTEN_RSTIEN_Msk |
                            HSUSBD_BUSINTEN_SUSPENDIEN_Msk |
                            HSUSBD_BUSINTEN_SOFIEN_Msk);
      HSUSBD_CLR_BUS_INT_FLAG(HSUSBD_BUSINTSTS_RESUMEIF_Msk);
    }

    if (IrqSt & HSUSBD_BUSINTSTS_SUSPENDIF_Msk)
    {
      usbHidOnSuspend();
      HSUSBD_ENABLE_BUS_INT(HSUSBD_BUSINTEN_RSTIEN_Msk |
                            HSUSBD_BUSINTEN_RESUMEIEN_Msk |
                            HSUSBD_BUSINTEN_SOFIEN_Msk);
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

  /* Keyboard interrupt-IN endpoint (EPA) */
  if (IrqStL & HSUSBD_GINTSTS_EPAIF_Msk)
  {
    IrqSt = HSUSBD->EP[EPA].EPINTSTS & HSUSBD->EP[EPA].EPINTEN;

    HSUSBD_ENABLE_EP_INT(EPA, 0);
    /* 데이터 패킷 전송 완료(TXPKIF) 시점에만 완료 처리 */
    if (IrqSt & HSUSBD_EPINTSTS_TXPKIF_Msk)
      usbHidEpHandler();
    HSUSBD_CLR_EP_INT_FLAG(EPA, IrqSt);
  }
}

#endif /* _USE_HW_USB */
