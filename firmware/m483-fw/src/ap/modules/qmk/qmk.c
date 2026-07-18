/*
 * qmk.c — QMK 코어 구동 (M483, USB 전용).
 *
 *  qmkInit()  : eeprom/VIA 초기화 -> USB 드라이버 등록 -> keyboard_setup/init
 *  qmkUpdate(): keyboard_task -> eeprom_task -> usbHidFlush (슈퍼루프에서 매 회 호출)
 *
 *  전송은 host_driver_t(port/driver_usb.c) 로만 이뤄진다(QMK 네이티브 드라이버 방식).
 *  BLE/RGB suspend/VENOM 텔레메트리는 Phase B 로 분리.
 */

#include "qmk.h"
#include "host.h"
#include "eeprom.h"
#include "via_hid.h"
#include "usbd_hid.h"
#include "cli.h"
#include "log.h"


extern host_driver_t usb_driver;   /* port/driver_usb.c */

static void cliQmk(cli_args_t *args);


bool qmkInit(void)
{
  eeprom_init();
  via_hid_init();

  host_set_driver(&usb_driver);

  keyboard_setup();
  keyboard_init();

  logPrintf("[  ] qmkInit()\n");
  logPrintf("     MATRIX %d x %d, DEBOUNCE %d\n", MATRIX_ROWS, MATRIX_COLS, DEBOUNCE);

  cliAdd("qmk", cliQmk);
  return true;
}

void qmkUpdate(void)
{
  keyboard_task();
  eeprom_task();
  usbHidFlush();   /* SOF/DataIn 이 멈춰도 큐가 비워지도록 하는 폴백 */
}

static void cliQmk(cli_args_t *args)
{
  bool ret = false;

  if (args->argc == 2 && args->isStr(0, "clear") && args->isStr(1, "eeprom"))
  {
    eeconfig_init();
    cliPrintf("Clearing EEPROM\n");
    ret = true;
  }

  if (ret == false)
  {
    cliPrintf("qmk clear eeprom\n");
  }
}
