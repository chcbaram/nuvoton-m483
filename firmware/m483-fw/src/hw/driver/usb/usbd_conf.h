/*
 * usbd_conf.h
 *
 *  HSUSBD 저수준 백엔드 (PHY/클럭 bring-up, HSUSBD_Open/Start, USBD20_IRQHandler).
 *  baram-qmk-8k 의 usb/usbd_conf.* 위치/역할을 미러링 (ST PCD -> Nuvoton HSUSBD).
 */

#ifndef SRC_HW_DRIVER_USB_USBD_CONF_H_
#define SRC_HW_DRIVER_USB_USBD_CONF_H_

#ifdef __cplusplus
extern "C" {
#endif

#include "hw_def.h"

#ifdef _USE_HW_USB

/* PHY/클럭 활성화 + HSUSBD_Open + EP 구성 + IRQ enable + Start */
void usbdConfInit(void);

/* SET_CONFIGURATION 완료 여부 */
bool usbdConfIsConfigured(void);

#endif /* _USE_HW_USB */

#ifdef __cplusplus
}
#endif

#endif /* SRC_HW_DRIVER_USB_USBD_CONF_H_ */
