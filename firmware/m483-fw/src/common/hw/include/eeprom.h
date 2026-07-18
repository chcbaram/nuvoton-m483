#ifndef EEPROM_H_
#define EEPROM_H_

#ifdef __cplusplus
 extern "C" {
#endif



#include "hw_def.h"

#ifdef _USE_HW_EEPROM


bool     eepromInit();
bool     eepromIsInit(void);
bool     eepromValid(uint32_t addr);
bool     eepromReadByte(uint32_t addr, uint8_t *p_data);
bool     eepromWriteByte(uint32_t addr, uint8_t data_in);   /* 블로킹(직접 호출용) */
bool     eepromRead(uint32_t addr, uint8_t *p_data, uint32_t length);
bool     eepromWrite(uint32_t addr, uint8_t *p_data, uint32_t length);

/* 논블로킹 쓰기(슈퍼루프 드레인용):
 *   eepromIsReady()    - write-cycle 완료/idle 이면 true (단일 ACK 프로브, 대기 없음)
 *   eepromWriteByteNb()- 쓰기만 시작하고 즉시 반환. 호출 전 eepromIsReady() 확인 필요. */
bool     eepromIsReady(void);
bool     eepromWriteByteNb(uint32_t addr, uint8_t data_in);
uint32_t eepromGetLength(void);
bool     eepromFormat(void);


#endif


#ifdef __cplusplus
}
#endif

#endif 