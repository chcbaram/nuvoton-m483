#include "ap.h"


/* qmk.c (QMK 코어). quantum.h 를 ap.c 로 끌어오지 않도록 전방 선언만 둔다. */
extern bool qmkInit(void);
extern void qmkUpdate(void);


void apInit(void)
{
  cliOpen(HW_UART_CH_CLI, 115200);
  cliLogo();

  usbBegin(USB_HID_MODE);   /* HSUSBD 3-인터페이스 HID 열거 시작 */
  qmkInit();                /* QMK 코어 + 매트릭스(keys.c) + VIA + EEPROM */
}

void apMain(void)
{
  uint32_t pre_time;

  pre_time = millis();
  while (1)
  {
    if (millis() - pre_time >= 500)
    {
      pre_time = millis();
      ledToggle(_DEF_LED1);
    }

    qmkUpdate();   /* 매 회 matrix_scan -> action -> USB (저지연, sleep 없음) */
    cliMain();
  }
}
