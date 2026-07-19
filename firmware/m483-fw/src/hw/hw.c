#include "hw.h"
#include QMK_KEYMAP_CONFIG_H   /* 키보드 config.h -> KBD_NAME (부트로더 INFO 표시용) */



extern uint32_t _fw_flash_begin;

/* name_str 에는 QMK 키보드 이름(KBD_NAME)을 심는다. m483-boot 가 firm_ver 를 읽어
 * INFO_UF2.TXT 에 표시(convex-qmk 와 동일: .name_str = KBD_NAME). */
volatile const firm_ver_t firm_ver __attribute__((section(".version"))) =
{
  .magic_number = VERSION_MAGIC_NUMBER,
  .version_str  = _DEF_FIRMWATRE_VERSION,
  .name_str     = KBD_NAME,
  .firm_addr    = (uint32_t)&_fw_flash_begin
};



bool hwInit(void)
{  
  cliInit();
  logInit();
  ledInit();
  uartInit();
  for (int i=0; i<HW_UART_MAX_CH; i++)
  {
    uartOpen(i, 115200);
  }

  logOpen(HW_LOG_CH, 115200);
  logPrintf("\r\n[ Firmware Begin... ]\r\n");
  logPrintf("Booting..Name \t\t: %s\r\n", KBD_NAME);
  logPrintf("Booting..Ver  \t\t: %s\r\n", _DEF_FIRMWATRE_VERSION);
  logPrintf("Booting..Clock\t\t: %d Mhz\r\n", (int)CLK_GetPLLClockFreq()/1000000);
  logPrintf("\n");

  resetInit();
  buttonInit();
  i2cInit();
  eepromInit();
  ws2812Init();
  /* keysInit(cfg) 는 QMK matrix_init() 가 보드 핀맵으로 호출한다 (port/matrix.c). */
  usbInit();

  return true;
}