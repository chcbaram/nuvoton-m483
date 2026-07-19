/*
 * M480 FMC 내부 APROM 플래시. erase = 4K 페이지, program = 4바이트 워드 단위(FMC_Write). FMC ISP 레지스터는 쓰기보호 대상이라
 * SYS_UnlockReg() 를 유지해야 한다.
 */
#include "flash.h"

#ifdef _USE_HW_FLASH

#include <string.h>


bool flashInit(void)
{
  SYS_UnlockReg();
  FMC_Open();
  FMC_ENABLE_AP_UPDATE();   /* 이 이미지에서 APROM 자가 기록 허용 */
  return true;
}

bool flashErase(uint32_t addr, uint32_t length)
{
  if (length == 0)
    return true;

  uint32_t page = addr & ~(FLASH_PAGE_SIZE - 1u);
  uint32_t end  = addr + length;

  if (end > FLASH_APROM_END)
    return false;

  SYS_UnlockReg();
  for (; page < end; page += FLASH_PAGE_SIZE)
  {
    if (FMC_Erase(page) != 0)
      return false;
  }
  return true;
}

bool flashWrite(uint32_t addr, uint8_t *p_data, uint32_t length)
{
  uint32_t idx = 0;
  uint32_t w;
  uint32_t v_addr = addr;      /* read-back 검증용 시작주소/길이 */
  uint32_t v_len  = length;

  if ((addr & 3u) != 0 || (addr + length) > FLASH_APROM_END)
    return false;

  SYS_UnlockReg();

  /* 신뢰성 우선 : 4바이트 워드 단위로 프로그램.
   * (M480 FMC_WriteMultiple 멀티워드 경로는 반환값을 과다계산해 안 써진 바이트를
   *  건너뛰는 갭이 생기는 사례가 있어 사용하지 않는다. FMC_Write 는 워드마다 ISPFF
   *  실패 플래그를 확인하므로 갭 없이 확실히 기록된다.) */
  while (length >= 4)
  {
    memcpy(&w, &p_data[idx], 4);
    if (FMC_Write(addr, w) != 0)
      return false;
    addr += 4; idx += 4; length -= 4;
  }

  /* 4바이트 미만 말단: 남는 바이트를 0xFF(erase 상태)로 패딩해 워드 단위 기록. */
  if (length > 0)
  {
    w = 0xFFFFFFFFu;
    memcpy(&w, &p_data[idx], length);
    if (FMC_Write(addr, w) != 0)
      return false;
  }

  /* read-back 검증 : 실제 써진 내용이 원본과 다르면(갭/실패) 즉시 실패 반환.
   * (자기참조 CRC는 못 잡는 write-time 손상을 여기서 잡는다.) */
  if (memcmp((const void *)v_addr, p_data, v_len) != 0)
    return false;

  return true;
}

bool flashRead(uint32_t addr, uint8_t *p_data, uint32_t length)
{
  memcpy(p_data, (const void *)addr, length);
  return true;
}

#endif
