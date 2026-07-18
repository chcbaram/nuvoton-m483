/*
 * usbd_hid.h
 *
 *  HSUSBD HID 복합장치 클래스 레이어 (QMK 네이티브 3-인터페이스).
 *   - 키보드(EPA), VIA(EPB IN/EPC OUT), Shared=NKRO+System+Consumer(EPD)
 *   - QMK 드라이버 방식(host_driver_t) 전송 API 를 제공: driver_usb.c 가 연결.
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
  uint32_t freq_hz;     /* 초당 수신 SOF 수 (HS ~8000) */
  uint32_t time_max;    /* SOF 간 최대 간격 (us) */
  uint32_t time_min;    /* SOF 간 최소 간격 (us) */
} usb_hid_rate_info_t;

/* Keystroke latency breakdown (us) */
typedef struct
{
  uint16_t raw_us;      /* 접점 -> USB TX 완료 */
  uint16_t pre_us;      /* 접점 -> 리포트 큐 적재 */
  uint16_t usb_us;      /* 큐 적재 -> USB TX 완료 */
  uint32_t seq;         /* 전송 시퀀스 카운터 */
} usb_hid_latency_t;

/* VIA raw-HID 수신 콜백 (OUT 완료 시, in-place 처리) */
typedef void (*usb_hid_via_rx_func_t)(uint8_t *data, uint8_t length);

/* USB 링크 헬스 (웹 대시보드 USB 점검용) */
typedef struct
{
  uint32_t reset_count;      /* USB 버스 리셋(재열거) 횟수 */
  uint32_t suspend_count;    /* USB 서스펜드 진입 횟수 */
  uint32_t sof_stall_count;  /* SOF(버스 프레임) 정지 감지 횟수 */
  uint32_t sof_rate;         /* 초당 수신 SOF 수 (HS ~8000) */
  uint32_t uptime_s;         /* 부팅 이후 경과(초) - 리부트 감지용 */
} usb_link_health_t;


/* ---- class core (usbd_conf 의 IRQ/Open 에서 호출) ---- */
void usbHidInit(void);          /* endpoint 구성 */
void HID_ClassRequest(void);    /* HID class setup 요청 처리 */
void HID_VendorRequest(void);   /* vendor 요청 (stub) */
void usbHidEpAHandler(void);    /* EPA 키보드 IN 완료(TXPKIF) */
void usbHidEpBHandler(void);    /* EPB VIA IN 완료(TXPKIF) */
void usbHidEpCHandler(void);    /* EPC VIA OUT 수신(RXPKIF) */
void usbHidEpDHandler(void);    /* EPD shared IN 완료(TXPKIF) */
void usbHidOnSof(void);
void usbHidOnBusReset(void);
void usbHidOnSuspend(void);

/* ---- QMK 드라이버 방식 전송 API ---- */
void    usbHidFlush(void);
bool    usbHidSendReport(uint8_t *p_data, uint16_t length);      /* EPA : 6KRO boot */
bool    usbHidSendReportNkro(uint8_t *p_data, uint16_t length);  /* EPD : NKRO */
bool    usbHidSendReportEXK(uint8_t *p_data, uint16_t length);   /* EPD : system/consumer */
bool    usbHidSendReportMouse(uint8_t *p_data, uint16_t length); /* EPD : mouse(report id 2) */
bool    usbHidSendReportVia(uint8_t *p_data, uint16_t length);   /* EPB : VIA 응답 */
void    usbHidSetViaReceiveFunc(usb_hid_via_rx_func_t fn);       /* VIA OUT 콜백 등록 */
uint8_t usbHidGetKbdLeds(void);                                  /* host LED 상태 */
bool    usbHidKbdIsReportProtocol(void);                         /* NKRO 가능(=report protocol) */
bool    usbHidNkroActive(void);                                  /* 현재 NKRO 활성(weak, QMK가 재정의) */
bool    usbHidIsReady(void);                                     /* 열거 완료 여부 */

/* ---- 측정 ---- */
bool usbHidGetRateInfo(usb_hid_rate_info_t *p_info);
bool usbHidGetLatency(uint16_t *raw_us, uint16_t *pre_us, uint16_t *usb_us, uint32_t *seq);
bool usbHidSetPressTime(uint32_t time_us);   /* 접점 시각 -> 레이턴시 기준 */

/* USB 링크 헬스 (웹 USB 점검) */
void usbHidGetLinkHealth(usb_link_health_t *p_info);
void usbHidResetLinkHealth(void);
void usbLinkFramePoll(void);   /* SOF 순단 감지 (메인루프에서 주기 호출) */

/* 호스트 LED 콜백 (__weak, 필요 시 재정의) */
void usbHidSetStatusLed(uint8_t led_bits);

/* CLI 등록 */
void usbHidCliInit(void);

#endif /* _USE_HW_USB */

#ifdef __cplusplus
}
#endif

#endif /* SRC_HW_DRIVER_USB_USB_HID_USBD_HID_H_ */
