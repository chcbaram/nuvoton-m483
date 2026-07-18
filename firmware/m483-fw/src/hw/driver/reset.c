#include "reset.h"


#ifdef _USE_HW_RESET
#include "cli.h"

#if CLI_USE(HW_RESET)
static void cliReset(cli_args_t *args);
#endif

static bool     is_init    = false;
static uint32_t reset_bits = 0;
static uint32_t boot_mode  = 0;   /* 주: RTC 스페어 레지스터 미사용(RAM) -> 리셋 후 비영속 */


static const char *reset_bit_str[] =
  {
    "RESET_BIT_POWER",
    "RESET_BIT_PIN",
    "RESET_BIT_WDG",
    "RESET_BIT_SOFT",
    "RESET_BIT_ETC",
  };

static const char *mode_bit_str[] =
  {
    "MODE_BIT_BOOT",
    "MODE_BIT_UPDATE",
  };



bool resetInit(void)
{
  uint32_t rst_sts = SYS->RSTSTS;

  if (rst_sts & SYS_RSTSTS_PORF_Msk)
    reset_bits |= (1 << RESET_BIT_POWER);
  if (rst_sts & SYS_RSTSTS_PINRF_Msk)
    reset_bits |= (1 << RESET_BIT_PIN);
  if (rst_sts & SYS_RSTSTS_WDTRF_Msk)
    reset_bits |= (1 << RESET_BIT_WDG);
  if (rst_sts & SYS_RSTSTS_SYSRF_Msk)
    reset_bits |= (1 << RESET_BIT_SOFT);

  /* write-1-clear : 확인한 리셋 플래그 클리어 */
  SYS->RSTSTS = rst_sts;

  is_init = true;

#if CLI_USE(HW_RESET)
  cliAdd("reset", cliReset);
#endif

  return is_init;
}

void resetLog(void)
{
  logPrintf("Reset Bits\r\n");
  for (int i = 0; i < RESET_BIT_MAX; i++)
  {
    if (reset_bits & (1 << i))
      logPrintf("     %s\r\n", reset_bit_str[i]);
  }
}

void resetToBoot(void)
{
  resetSetBootMode(1 << MODE_BIT_BOOT);
  resetToReset();
}

void resetToReset(void)
{
  NVIC_SystemReset();
}

uint32_t resetGetBits(void)
{
  return reset_bits;
}

void resetSetBits(uint32_t data)
{
  reset_bits = data;
}

void resetSetBootMode(uint32_t data)
{
  boot_mode = data;
}

uint32_t resetGetBootMode(void)
{
  return boot_mode;
}


#if CLI_USE(HW_RESET)
void cliReset(cli_args_t *args)
{
  bool ret = false;

  if (args->argc == 1 && args->isStr(0, "info"))
  {
    cliPrintf("Reset Bits\n");
    for (int i = 0; i < RESET_BIT_MAX; i++)
    {
      if (reset_bits & (1 << i))
        cliPrintf("      %s\n", reset_bit_str[i]);
    }
    for (int i = 0; i < MODE_BIT_MAX; i++)
    {
      if (boot_mode & (1 << i))
        cliPrintf("      %s\n", mode_bit_str[i]);
    }
    ret = true;
  }

  if (args->argc == 1 && args->isStr(0, "reset"))
  {
    resetToReset();
    ret = true;
  }

  if (ret == false)
  {
    cliPrintf("reset info\n");
    cliPrintf("reset reset\n");
  }
}
#endif

#endif /* _USE_HW_RESET */
