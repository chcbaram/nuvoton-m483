#ifndef BOOT_H_
#define BOOT_H_

#include "ap_def.h"

#ifdef __cplusplus
extern "C" {
#endif

bool bootInit(void);

/* Application validity: completion tag present + body CRC match + vector sanity. */
bool bootIsFirmValid(void);

/* Write the completion firm_tag_t at FLASH_ADDR_TAG (called last, after verify). */
bool bootWriteTag(uint32_t fw_size, uint16_t fw_crc);

/* Quiesce peripherals and branch to the application. Returns only if the app
 * is invalid (caller should stay in bootloader). */
void bootJumpFirm(void);

#ifdef __cplusplus
}
#endif

#endif
