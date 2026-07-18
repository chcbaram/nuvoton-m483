/*
 * usbd_hid.h
 *
 *  HSUSBD HID keyboard class layer.
 *  공개 API 이름은 baram-qmk-8k 의 usb_hid/usbd_hid.h 를 미러링하여
 *  향후 QMK 포팅 시 호출부를 그대로 재사용할 수 있게 한다.
 */

#ifndef SRC_HW_DRIVER_USB_USB_HID_USBD_HID_H_
#define SRC_HW_DRIVER_USB_USB_HID_USBD_HID_H_

#ifdef __cplusplus
extern "C" {
#endif

#include "hw_def.h"

#ifdef _USE_HW_USB

#include "usbd_desc.h"


/* Host LED output report bits (SET_REPORT) */
enum
{
  USB_HID_LED_NUM_LOCK    = (1 << 0),
  USB_HID_LED_CAPS_LOCK   = (1 << 1),
  USB_HID_LED_SCROLL_LOCK = (1 << 2),
  USB_HID_LED_COMPOSE     = (1 << 3),
  USB_HID_LED_KANA        = (1 << 4),
};

/* Polling-rate / bus-frame measurement */
typedef struct
{
  uint32_t freq_hz;     /* 초당 수신 SOF 수 (버스 마이크로프레임 심박; HS ~8000, FS ~1000) */
  uint32_t time_max;    /* SOF 간 최대 간격 (us) */
  uint32_t time_min;    /* SOF 간 최소 간격 (us) */
} usb_hid_rate_info_t;

/* Keystroke latency breakdown (us) */
typedef struct
{
  uint16_t raw_us;      /* 접점(버튼) -> USB TX 완료 */
  uint16_t pre_us;      /* 접점 -> 리포트 큐 적재 (펌웨어) */
  uint16_t usb_us;      /* 큐 적재 -> USB TX 완료 (USB 구간) */
  uint32_t seq;         /* 전송 시퀀스 카운터 */
} usb_hid_latency_t;


/* ---- class core (usbd_conf 의 IRQ/Open 에서 호출) ---- */
void usbHidInit(void);          /* endpoint 구성 (BSP HID_Init 이식) */
void HID_ClassRequest(void);    /* HID class setup 요청 처리 */
void HID_VendorRequest(void);   /* vendor 요청 (stub) */
void usbHidEpHandler(void);     /* 키보드 EP(EPA) IN 완료 처리 (IRQ) */
void usbHidOnSof(void);         /* SOF 인터럽트 (폴링레이트 측정) */
void usbHidOnBusReset(void);    /* 버스 리셋 (링크 상태) */
void usbHidOnSuspend(void);     /* 서스펜드 (링크 상태) */

/* ---- 공개 API (baram 미러링) ---- */
void usbHidFlush(void);
bool usbHidSendReport(uint8_t *p_data, uint16_t length);
bool usbHidGetRateInfo(usb_hid_rate_info_t *p_info);
bool usbHidGetLatency(uint16_t *raw_us, uint16_t *pre_us, uint16_t *usb_us, uint32_t *seq);

/* 접점(버튼 눌림/뗌) 발생 시각을 us 로 전달 -> 레이턴시 계산 기준 */
bool usbHidSetPressTime(uint32_t time_us);

/* 호스트 LED(NumLock/CapsLock..) 상태 콜백 (__weak, 필요 시 재정의) */
void usbHidSetStatusLed(uint8_t led_bits);

/* CLI 등록 */
void usbHidCliInit(void);

#endif /* _USE_HW_USB */

#ifdef __cplusplus
}
#endif

#endif /* SRC_HW_DRIVER_USB_USB_HID_USBD_HID_H_ */
