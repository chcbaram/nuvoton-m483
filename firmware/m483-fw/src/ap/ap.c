#include "ap.h"


static void apKeyUpdate(void);


void apInit(void)
{
  cliOpen(HW_UART_CH_CLI, 115200);
  cliLogo();

  usbBegin(USB_HID_MODE);
}

void apMain(void)
{
  uint32_t pre_time;

  pre_time = millis();
  while(1)
  {
    if (millis()-pre_time >= 500)
    {
      pre_time = millis();
      ledToggle(_DEF_LED1);
    }

    apKeyUpdate();
    usbHidFlush();

    cliMain();
  }
}

/*
 * BTN1(PC.14) 을 스캔하여 눌림/뗌 에지에 HID 키('a', keycode 0x04) 리포트를 전송.
 * eager 디바운스: 상태가 바뀌면 즉시 전송하고, 이후 8ms 동안 재감지를 잠근다(lockout).
 * 접점 시각을 micros() 로 캡처하여 레이턴시 측정 기준으로 전달한다.
 */
#define KEY_DEBOUNCE_MS   8

static void apKeyUpdate(void)
{
  static bool     cur_pressed  = false;
  static uint32_t lock_time_ms = 0;
  static bool     is_locked    = false;

  /* 직전 전송 후 8ms 잠금 구간이면 재감지하지 않음 */
  if (is_locked)
  {
    if ((millis() - lock_time_ms) < KEY_DEBOUNCE_MS)
      return;
    is_locked = false;
  }

  bool raw = buttonGetPressed(_DEF_BUTTON1);

  if (raw != cur_pressed)
  {
    uint8_t report[8] = {0, };

    /* 접점 시각 = 감지 즉시 (지연 없이 바로 전송) */
    usbHidSetPressTime(micros());

    cur_pressed = raw;
    if (cur_pressed)
      report[2] = 0x04;                 /* HID keycode 'a' */

    usbHidSendReport(report, 8);

    /* 전송 직후 8ms 잠금 시작 */
    lock_time_ms = millis();
    is_locked    = true;
  }
}
