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
#ifdef DEBOUNCE_RUNTIME
#include "debounce_cfg.h"
#endif
#ifdef KILL_SWITCH_ENABLE
#include "kill_switch.h"
#endif
#ifdef KKUK_ENABLE
#include "kkuk.h"
#endif
#ifdef HOLD_OKP_RUNTIME
#include "hold_okp.h"
#endif
#ifdef RGB_MATRIX_ENABLE
#include "rgb_matrix.h"
#include "ws2812.h"
#endif


extern host_driver_t usb_driver;   /* port/driver_usb.c */

static void cliQmk(cli_args_t *args);
static void cliRgb(cli_args_t *args);

/* keyboard_task() 처리시간 계측 (matrix_scan + action + rgb_matrix_task 포함).
 * RGB on/off 로 이 값을 비교하면 RGB 가 슈퍼루프 CPU 에 주는 영향을 알 수 있다. */
static volatile uint32_t task_us_last = 0;
static volatile uint32_t task_us_max  = 0;
static volatile uint32_t task_us_sum  = 0;
static volatile uint32_t task_us_cnt  = 0;


/* QMK keyboard_init 이후 훅 : 런타임 설정을 eeconfig 에서 로드/적용 */
void keyboard_post_init_user(void)
{
#ifdef DEBOUNCE_RUNTIME
  debounce_cfg_init();
#endif
#ifdef KILL_SWITCH_ENABLE
  kill_switch_init();
#endif
#ifdef KKUK_ENABLE
  kkuk_init();
#endif
#ifdef HOLD_OKP_RUNTIME
  hold_okp_init();
#endif
}

/* 키 이벤트 훅 : SOCD(상반키) 처리 + 꾹(터보) 카운트 */
bool process_record_user(uint16_t keycode, keyrecord_t *record)
{
#ifdef KILL_SWITCH_ENABLE
  kill_switch_process(keycode, record);
#endif
#ifdef KKUK_ENABLE
  kkuk_process(keycode, record);
#endif
  return true;
}


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
  cliAdd("rgb", cliRgb);
  return true;
}

void qmkUpdate(void)
{
  uint32_t t0 = micros();
  keyboard_task();                 /* matrix_scan + action + (RGB 시) rgb_matrix_task */
  uint32_t dt = micros() - t0;

  task_us_last = dt;
  if (dt > task_us_max) task_us_max = dt;
  task_us_sum += dt;
  task_us_cnt++;

#ifdef KKUK_ENABLE
  kkuk_idle();     /* 꾹(터보) 리피트 상태머신 */
#endif
#ifdef RGB_MATRIX_ENABLE
  ws2812Poll();    /* busy 로 skip 된 RGB 프레임(특히 꺼짐 시 black) 재전송 보장 */
#endif
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

/*
 * rgb : keyboard_task() 처리시간(=RGB 부하 포함) 및 RGB 상태/제어.
 *   RGB on/off 로 task 시간을 비교하면 RGB 가 슈퍼루프 CPU 에 주는 영향을 알 수 있다.
 *   (keyboard_task 안에서 rgb_matrix_task 가 프레임 주기로 렌더/flush 한다)
 */
static void cliRgb(cli_args_t *args)
{
  bool ret = false;

  if (args->argc == 1 && args->isStr(0, "info"))
  {
    uint32_t avg = task_us_cnt ? (task_us_sum / task_us_cnt) : 0;

    cliPrintf("keyboard_task : last %d us, avg %d us, max %d us  (n=%d)\n",
              (int)task_us_last, (int)avg, (int)task_us_max, (int)task_us_cnt);
#ifdef RGB_MATRIX_ENABLE
    cliPrintf("rgb matrix    : %s, mode %d, val %d, leds %d\n",
              rgb_matrix_is_enabled() ? "ON" : "off",
              (int)rgb_matrix_get_mode(), (int)rgb_matrix_get_val(),
              (int)RGB_MATRIX_LED_COUNT);
    cliPrintf("               (RGB on/off 로 keyboard_task 시간 비교 -> RGB CPU 부하)\n");
#else
    cliPrintf("rgb matrix    : not built\n");
#endif
    ret = true;
  }

  if (args->argc == 1 && args->isStr(0, "reset"))
  {
    task_us_last = task_us_max = 0;
    task_us_sum  = task_us_cnt = 0;
    cliPrintf("task time stats reset\n");
    ret = true;
  }

  if (args->argc == 1 && args->isStr(0, "rate"))
  {
    cliPrintf("keyboard_task time (Ctrl-C 종료)\n");
    while (cliKeepLoop())
    {
      uint32_t avg = task_us_cnt ? (task_us_sum / task_us_cnt) : 0;
      cliPrintf("last %d us, avg %d us, max %d us\r",
                (int)task_us_last, (int)avg, (int)task_us_max);
      delay(200);
    }
    cliPrintf("\n");
    ret = true;
  }

#ifdef RGB_MATRIX_ENABLE
  if (args->argc == 1 && args->isStr(0, "on"))
  {
    rgb_matrix_enable();
    cliPrintf("rgb on\n");
    ret = true;
  }
  if (args->argc == 1 && args->isStr(0, "off"))
  {
    rgb_matrix_disable();
    cliPrintf("rgb off\n");
    ret = true;
  }
  if (args->argc == 2 && args->isStr(0, "mode"))
  {
    uint8_t m = (uint8_t)args->getData(1);
    rgb_matrix_mode(m);
    cliPrintf("rgb mode %d\n", m);
    ret = true;
  }
#endif

  if (ret == false)
  {
    cliPrintf("rgb info\n");
    cliPrintf("rgb rate\n");
    cliPrintf("rgb reset\n");
#ifdef RGB_MATRIX_ENABLE
    cliPrintf("rgb on|off\n");
    cliPrintf("rgb mode [n]\n");
#endif
  }
}
