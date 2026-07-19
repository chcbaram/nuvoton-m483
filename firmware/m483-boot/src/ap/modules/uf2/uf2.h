/*
 * Microsoft UF2 - MIT License (Microsoft Corporation)
 * Adapted for m483-boot: direct-to-application-region programming on M483 FMC.
 */
#ifndef UF2FORMAT_H
#define UF2FORMAT_H 1

#include "uf2_def.h"

#ifdef __cplusplus
extern "C" {
#endif

/* Largest span the exposed FS advertises / the writtenMask must cover. */
#ifndef CFG_UF2_FLASH_SIZE
#define CFG_UF2_FLASH_SIZE   FLASH_SIZE_FIRM
#endif

/* All entries little endian. */
#define UF2_MAGIC_START0     0x0A324655UL   /* "UF2\n"          */
#define UF2_MAGIC_START1     0x9E5D5157UL   /* randomly selected */
#define UF2_MAGIC_END        0x0AB16F30UL

#define UF2_FLAG_NOFLASH     0x00000001
#define UF2_FLAG_FAMILYID    0x00002000

#define MAX_BLOCKS           (CFG_UF2_FLASH_SIZE / 256 + 100)

typedef struct
{
  uint32_t numBlocks;
  uint32_t numWritten;
  uint8_t  writtenMask[MAX_BLOCKS / 8 + 1];
} WriteState;

typedef struct
{
  /* 32 byte header */
  uint32_t magicStart0;
  uint32_t magicStart1;
  uint32_t flags;
  uint32_t targetAddr;
  uint32_t payloadSize;
  uint32_t blockNo;
  uint32_t numBlocks;
  uint32_t familyID;

  uint8_t  data[476];

  uint32_t magicEnd;
} UF2_Block;

void uf2Init(void);
void uf2Update(void);

/* Process one 512-byte MSC sector. Call from the MAIN LOOP (consumer), never
 * from the USB IRQ - it performs blocking FMC erase/program.
 *   returns -1 : not a UF2 block (FAT metadata) - ignore, ACK to host
 *            0 : UF2 block out of the app window - ignored, ACK to host
 *            1 : programmed
 */
int  uf2_write_block(uint8_t *data);

bool uf2IsBusy(void);   /* a flashing session is in progress */

#ifdef __cplusplus
}
#endif

#endif
