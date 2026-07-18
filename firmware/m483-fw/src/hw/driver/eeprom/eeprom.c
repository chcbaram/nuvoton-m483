/*
 * eeprom.c
 *
 *  I2C EEPROM (ZD24C128, 16KB) 드라이버. 회로도 U18 = ZD24C128A-XGMB,
 *  I2C1 (SCL=PA.7, SDA=PA.6), slave addr 0x50.
 *  i2c.h API 위에서 동작 (baram-qmk-8k eeprom/zd24c128.c 이식).
 */

#include "eeprom.h"


#if defined(_USE_HW_EEPROM) && defined(EEPROM_CHIP_ZD24C128)
#include "i2c.h"
#include "cli.h"


#if CLI_USE(HW_EEPROM)
static void cliEeprom(cli_args_t *args);
#endif


#define EEPROM_MAX_SIZE   (16*1024)


static bool    is_init  = false;
static uint8_t i2c_ch   = _DEF_I2C1;
static uint8_t i2c_addr = 0x50;




bool eepromInit()
{
  bool ret;


  ret = i2cBegin(i2c_ch, 400);

  if (ret == true)
  {
    ret = eepromValid(0x00);
  }

  logPrintf("[%s] eepromInit()\n", ret ? "OK" : "E_");
  if (ret == true)
  {
    logPrintf("     chip  : ZD24C128\n");
    logPrintf("     found : 0x%02X\n", i2c_addr);
    logPrintf("     size  : %dKB\n", (int)(eepromGetLength() / 1024));
  }
  else
  {
    logPrintf("     empty\n");
  }

#if CLI_USE(HW_EEPROM)
  cliAdd("eeprom", cliEeprom);
#endif

  is_init = ret;

  return ret;
}

bool eepromIsInit(void)
{
  return is_init;
}

bool eepromValid(uint32_t addr)
{
  uint8_t data;
  bool    ret;

  if (addr >= EEPROM_MAX_SIZE)
    return false;

  ret = i2cReadA16Bytes(i2c_ch, i2c_addr, addr, &data, 1, 100);

  return ret;
}

bool eepromReadByte(uint32_t addr, uint8_t *p_data)
{
  bool ret;

  if (addr >= EEPROM_MAX_SIZE)
    return false;

  ret = i2cReadA16Bytes(i2c_ch, i2c_addr, addr, p_data, 1, 100);

  return ret;
}

/* write-cycle 완료/idle 여부 (단일 ACK 프로브, 블로킹 없음) */
bool eepromIsReady(void)
{
  if (is_init == false)
    return false;
  return i2cIsDeviceReady(i2c_ch, i2c_addr);
}

/* 논블로킹: 쓰기만 시작(write cycle 진행), 완료 대기 안 함.
 * 호출 전 eepromIsReady() 로 직전 write 완료를 확인해야 한다. */
bool eepromWriteByteNb(uint32_t addr, uint8_t data_in)
{
  if (addr >= EEPROM_MAX_SIZE)
    return false;

  return i2cWriteA16Bytes(i2c_ch, i2c_addr, addr, &data_in, 1, 10);
}

/* 블로킹(직접 호출용): 직전 write 완료 대기 -> 쓰기 -> 이번 cycle 완료 대기. */
bool eepromWriteByte(uint32_t addr, uint8_t data_in)
{
  uint32_t pre_time;

  if (addr >= EEPROM_MAX_SIZE)
    return false;

  /* 직전 write(비동기 Nb 포함)가 진행 중이면 완료 대기 */
  pre_time = millis();
  while (eepromIsReady() == false)
  {
    if (millis() - pre_time >= 100)
      return false;
    delay(1);
  }

  if (eepromWriteByteNb(addr, data_in) == false)
    return false;

  /* 이번 write cycle 완료까지 ACK polling (최대 100ms) */
  pre_time = millis();
  while (millis() - pre_time < 100)
  {
    if (eepromIsReady() == true)
      return true;
    delay(1);
  }

  return false;
}

bool eepromRead(uint32_t addr, uint8_t *p_data, uint32_t length)
{
  bool     ret = true;
  uint32_t i;

  for (i = 0; i < length; i++)
  {
    ret = eepromReadByte(addr + i, &p_data[i]);
    if (ret != true)
      break;
  }

  return ret;
}

bool eepromWrite(uint32_t addr, uint8_t *p_data, uint32_t length)
{
  bool     ret = false;
  uint32_t i;

  for (i = 0; i < length; i++)
  {
    ret = eepromWriteByte(addr + i, p_data[i]);
    if (ret == false)
      break;
  }

  return ret;
}

uint32_t eepromGetLength(void)
{
  return EEPROM_MAX_SIZE;
}

bool eepromFormat(void)
{
  return true;
}




#if CLI_USE(HW_EEPROM)
static void cliEeprom(cli_args_t *args)
{
  bool     ret = true;
  uint32_t i;
  uint32_t addr;
  uint32_t length;
  uint8_t  data;
  uint32_t pre_time;
  bool     eep_ret;


  if (args->argc == 1)
  {
    if (args->isStr(0, "info") == true)
    {
      cliPrintf("eeprom init   : %s\n", eepromIsInit() ? "True" : "False");
      cliPrintf("eeprom length : %d bytes\n", (int)eepromGetLength());
    }
    else if (args->isStr(0, "format") == true)
    {
      cliPrintf(eepromFormat() ? "format OK\n" : "format Fail\n");
    }
    else
    {
      ret = false;
    }
  }
  else if (args->argc == 3)
  {
    if (args->isStr(0, "read") == true)
    {
      addr   = (uint32_t)args->getData(1);
      length = (uint32_t)args->getData(2);

      if (length > eepromGetLength())
        cliPrintf("length error\n");

      for (i = 0; i < length; i++)
      {
        if (eepromReadByte(addr + i, &data) == true)
        {
          cliPrintf("addr : %d\t 0x%02X\n", (int)(addr + i), data);
        }
        else
        {
          cliPrintf("eepromReadByte() Error\n");
          break;
        }
      }
    }
    else if (args->isStr(0, "write") == true)
    {
      addr = (uint32_t)args->getData(1);
      data = (uint8_t)args->getData(2);

      pre_time = millis();
      eep_ret  = eepromWriteByte(addr, data);

      cliPrintf("addr : %d\t 0x%02X %dms\n", (int)addr, data, (int)(millis() - pre_time));
      cliPrintf(eep_ret ? "OK\n" : "FAIL\n");
    }
    else
    {
      ret = false;
    }
  }
  else
  {
    ret = false;
  }


  if (ret == false)
  {
    cliPrintf("eeprom info\n");
    cliPrintf("eeprom format\n");
    cliPrintf("eeprom read  [addr] [length]\n");
    cliPrintf("eeprom write [addr] [data]\n");
  }
}
#endif


#endif /* _USE_HW_EEPROM && EEPROM_CHIP_ZD24C128 */
