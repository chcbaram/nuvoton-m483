/*
 * 부트 진입 플래그를 reset.c 에 캡슐화(baram 구조). baram은 RTC 백업 레지스터를
 * 쓰지만, 여기서는 고정 주소 retained-SRAM(BOOT_FLAG_ADDR)을 사용한다.
 * - 앱->부트 진입은 NVIC_SystemReset(웜 리셋)이라 SRAM이 보존됨 (RTC 크리스털 불필요).
 * - resetInit()에서 한 번 읽고 즉시 클리어(1회성). 부트/앱이 같은 고정 주소를 공유.
 */
#include "reset.h"

#ifdef _USE_HW_RESET


static volatile uint32_t *p_boot_flag = (volatile uint32_t *)BOOT_FLAG_ADDR;  /* [0]=magic [1]=mode */

static uint32_t reset_bits = 0;
static uint32_t boot_mode  = 0;


bool resetInit(void)
{
  uint32_t rst_sts = SYS->RSTSTS;

  if (rst_sts & SYS_RSTSTS_PORF_Msk)  reset_bits |= (1 << RESET_BIT_POWER);
  if (rst_sts & SYS_RSTSTS_PINRF_Msk) reset_bits |= (1 << RESET_BIT_PIN);
  if (rst_sts & SYS_RSTSTS_WDTRF_Msk) reset_bits |= (1 << RESET_BIT_WDG);
  if (rst_sts & SYS_RSTSTS_SYSRF_Msk) reset_bits |= (1 << RESET_BIT_SOFT);
  SYS->RSTSTS = rst_sts;

  /* 영속 플래그 1회 소비 : POR 시 retained SRAM은 미정의이므로 magic으로 유효성 확인. */
  if (p_boot_flag[0] == BOOT_REQUEST_MAGIC)
    boot_mode = p_boot_flag[1];
  p_boot_flag[0] = 0;
  p_boot_flag[1] = 0;

  return true;
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
  boot_mode      = data;
  p_boot_flag[0] = BOOT_REQUEST_MAGIC;
  p_boot_flag[1] = data;
}

uint32_t resetGetBootMode(void)
{
  return boot_mode;
}

void resetLog(void)
{
}

#endif
