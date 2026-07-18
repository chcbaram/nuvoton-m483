/*
 * USB 출력용 host_driver_t.
 *
 * host.c 의 host_*_send() 가 활성 드라이버의 함수 포인터로 dispatch 하며,
 * 여기서 M483 HSUSBD HID 전송 API(usbd_hid.c)로 연결한다.
 * (qmk-zephyr 의 port/driver_usb.c 를 M483 다중 인터페이스용으로 이식)
 *
 *   send_keyboard : 6KRO boot 리포트  -> 키보드 인터페이스(EPA)
 *   send_nkro     : NKRO 비트맵 리포트 -> 키보드 인터페이스(EPA, report-protocol)
 *   send_extra    : system/consumer   -> EXK 인터페이스(EPD)
 *   send_mouse    : Phase A 미연결(stub)
 */

#include "host.h"
#include "host_driver.h"
#include "report.h"
#include "usb_hid/usbd_hid.h"

static uint8_t usb_keyboard_leds(void)
{
  return usbHidGetKbdLeds();
}

static void usb_send_keyboard(report_keyboard_t *report)
{
  usbHidSendReport((uint8_t *)report, KEYBOARD_REPORT_SIZE);
}

static void usb_send_nkro(report_nkro_t *report)
{
#ifdef NKRO_ENABLE
  usbHidSendReportNkro((uint8_t *)report, sizeof(report_nkro_t));
#else
  (void)report;
#endif
}

static void usb_send_mouse(report_mouse_t *report)
{
  /* Phase A: 마우스 리포트 미연결 */
  (void)report;
}

static void usb_send_extra(report_extra_t *report)
{
#ifdef EXTRAKEY_ENABLE
  usbHidSendReportEXK((uint8_t *)report, sizeof(report_extra_t));
#else
  (void)report;
#endif
}

host_driver_t usb_driver = {
  usb_keyboard_leds,
  usb_send_keyboard,
  usb_send_nkro,
  usb_send_mouse,
  usb_send_extra,
};

/*
 * 현재 USB 키보드 프로토콜 접근자 (1=report/NKRO 가능, 0=boot).
 * host.h 가 stock 참조명 keyboard_protocol 을 이 함수로 매핑한다.
 * usbd_hid 의 SET_PROTOCOL 추적값을 실시간으로 읽는다.
 */
uint8_t keyboard_protocol_get(void)
{
  return usbHidKbdIsReportProtocol() ? 1 : 0;
}

/*
 * NKRO 가능 판정 : USB 키보드 인터페이스가 report protocol 일 때만 true.
 * 실제 NKRO 활성은 이 값 && keymap_config.nkro (NK_TOGG 로 토글).
 */
bool host_can_send_nkro(void)
{
  return usbHidKbdIsReportProtocol();
}
