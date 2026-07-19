#include "hw.h"



extern uint32_t _fw_flash_begin;

volatile const firm_ver_t firm_ver __attribute__((section(".version"))) = 
{
  .magic_number = VERSION_MAGIC_NUMBER,
  .version_str  = _DEF_FIRMWATRE_VERSION,
  .name_str     = _DEF_BOARD_NAME,
  .firm_addr    = (uint32_t)&_fw_flash_begin
};



bool hwInit(void)
{
  ledInit();

  uartInit();
  logInit();
  logOpen(HW_LOG_CH, 115200);

  resetInit();
  flashInit();
  keysInit();

  logPrintf("\r\n");
  logPrintf("[ WISH-BOOT ] UF2 Bootloader\r\n");
  logPrintf("Boot Ver  : %s\r\n", _DEF_FIRMWATRE_VERSION);
  logPrintf("Board     : %s\r\n", _DEF_BOARD_NAME);
  logPrintf("Reset     : 0x%02X\r\n", (unsigned)resetGetBits());
  logPrintf("App @     : 0x%08X\r\n", (unsigned)FLASH_ADDR_FIRM);

  return true;
}