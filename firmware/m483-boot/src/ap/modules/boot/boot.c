/*
 * 앱 유효성 검사 + 앱 점프.
 *   FLASH_ADDR_FIRM(0xC000) : 앱 이미지(표준 벡터테이블 선두)
 *   FLASH_ADDR_TAG (0x7F000): 검증 완료 시 부트로더가 기록한 firm_tag_t
 */
#include "boot.h"
#include "flash.h"
#include "util_core.h"
#include <string.h>


static uint16_t tag_body_crc(const firm_tag_t *p_tag)
{
  /* tag_crc 앞 필드들에 대한 CRC (태그 구조 자체 무결성). */
  return utilCalcCRC(0, (uint8_t *)p_tag,
                     sizeof(firm_tag_t) - sizeof(p_tag->tag_crc));
}

bool bootInit(void)
{
  SYS_UnlockReg();
  FMC_Open();
  FMC_ENABLE_AP_UPDATE();
  return true;
}

bool bootIsFirmValid(void)
{
  const firm_tag_t *p_tag = (const firm_tag_t *)FLASH_ADDR_TAG;
  uint32_t sp = *(volatile uint32_t *)(FLASH_ADDR_FIRM + 0);   /* 초기 MSP */
  uint32_t pc = *(volatile uint32_t *)(FLASH_ADDR_FIRM + 4);   /* reset 핸들러 */

  /* 1) 완료 태그 존재 + 자체 무결성 */
  if (p_tag->magic_number != TAG_MAGIC_NUMBER)
    return false;
  if (p_tag->tag_crc != tag_body_crc(p_tag))
    return false;
  if (p_tag->fw_addr != FLASH_ADDR_FIRM)
    return false;
  if (p_tag->fw_size == 0 || p_tag->fw_size > (FLASH_APP_LIMIT - FLASH_ADDR_FIRM))
    return false;

  /* 2) 벡터 sanity (SP는 SRAM 범위, reset 핸들러는 앱 범위 + thumb 비트) */
  if (sp < 0x20000000UL || sp > 0x20028000UL)
    return false;
  if (pc < FLASH_ADDR_FIRM || pc >= FLASH_APP_LIMIT || (pc & 1u) == 0u)
    return false;

  /* 3) 본문 CRC 재계산 == 태그값 */
  if (utilCalcCRC(0, (uint8_t *)FLASH_ADDR_FIRM, p_tag->fw_size) != (uint16_t)p_tag->fw_crc)
    return false;

  return true;
}

bool bootWriteTag(uint32_t fw_size, uint16_t fw_crc)
{
  firm_tag_t tag;

  memset(&tag, 0, sizeof(tag));
  tag.magic_number = TAG_MAGIC_NUMBER;
  tag.fw_addr      = FLASH_ADDR_FIRM;
  tag.fw_size      = fw_size;
  tag.fw_crc       = fw_crc;
  tag.tag_crc      = tag_body_crc(&tag);

  return flashWrite(FLASH_ADDR_TAG, (uint8_t *)&tag, sizeof(tag));
}

void bootJumpFirm(void)
{
  if (!bootIsFirmValid())
    return;

  uint32_t app_sp = *(volatile uint32_t *)(FLASH_ADDR_FIRM + 0);
  uint32_t app_pc = *(volatile uint32_t *)(FLASH_ADDR_FIRM + 4);

  /* quiesce : bootJumpFirm 은 usbInit 전에 호출되므로 SysTick/IRQ만 정리하면 된다. */
  __disable_irq();

  SysTick->CTRL = 0;
  SysTick->LOAD = 0;
  SysTick->VAL  = 0;

  for (int i = 0; i < 8; i++)
  {
    NVIC->ICER[i] = 0xFFFFFFFFUL;   /* 전체 IRQ disable + pending clear */
    NVIC->ICPR[i] = 0xFFFFFFFFUL;
  }

  /* 제어 이양 : VTOR/MSP 세팅 후 앱 reset 핸들러로 분기. */
  SCB->VTOR = FLASH_ADDR_FIRM;
  __set_MSP(app_sp);
  __enable_irq();                 /* 앱은 인터럽트 활성 상태로 시작해야 함 */

  ((void (*)(void))app_pc)();

  while (1) { }
}
