/*
 * usb.c
 *
 *  USB 상위 레이어. usbInit() 로 CLI 등록, usbBegin(mode) 로 실제 구동.
 *  현재는 HID(키보드) 모드만 구현. baram-qmk-8k 의 usb/usb.c 미러링.
 */

#include "usb.h"

#ifdef _USE_HW_USB

#include "cli.h"

static UsbMode_t usb_mode = USB_NON_MODE;
static bool      is_open  = false;

#ifdef _USE_HW_CLI
static void cliUsb(cli_args_t *args);
#endif


bool usbInit(void)
{
  usb_mode = USB_NON_MODE;
  is_open  = false;

#ifdef _USE_HW_CLI
  cliAdd("usb", cliUsb);
#endif

#if HW_USB_HID == 1
  usbHidCliInit();
#endif

  return true;
}

bool usbBegin(UsbMode_t mode)
{
  if (is_open == true)
    return true;

  switch (mode)
  {
#if HW_USB_HID == 1
    case USB_HID_MODE:
      usbdConfInit();
      usb_mode = USB_HID_MODE;
      is_open  = true;
      break;
#endif

    default:
      return false;
  }

  return is_open;
}

bool usbIsOpen(void)
{
  return is_open;
}

bool usbIsConnect(void)
{
  return usbdConfIsConfigured();
}

UsbMode_t usbGetMode(void)
{
  return usb_mode;
}


#ifdef _USE_HW_CLI
static void cliUsb(cli_args_t *args)
{
  bool ret = false;

  if (args->argc == 1 && args->isStr(0, "info"))
  {
    cliPrintf("usb mode    : %d\n", (int)usb_mode);
    cliPrintf("usb open    : %s\n", is_open ? "yes" : "no");
    cliPrintf("usb connect : %s\n", usbIsConnect() ? "yes" : "no");
    ret = true;
  }

  if (ret == false)
  {
    cliPrintf("usb info\n");
  }
}
#endif

#endif /* _USE_HW_USB */
