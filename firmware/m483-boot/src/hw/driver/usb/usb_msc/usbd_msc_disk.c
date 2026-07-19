/*
 * usbd_msc_disk.c
 *
 * 즉석 생성 FAT16 이미지. 레이아웃/파라미터는 convex-boot(검증된 부트로더)와 동일:
 * 16MB, cluster 1, FAT 2개(각 128섹터), root 512엔트리(32섹터). macOS/Win 마운트 검증됨.
 *   LBA 0 boot | 1..128 FAT1 | 129..256 FAT2 | 257..288 root | 289 INFO_UF2.TXT(cluster2)
 */
#include <string.h>
#include <stdio.h>

#include "usbd_msc_disk.h"
#include "uf2/uf2_def.h"


/* FAT 라벨(11B, 공백 패딩, NUL 없음). */
static void fat_label(uint8_t *out)
{
  size_t n = strlen(UF2_VOLUME_LABEL);
  if (n > 11)
    n = 11;
  memset(out, ' ', 11);
  memcpy(out, UF2_VOLUME_LABEL, n);
}

static uint32_t build_info_txt(uint8_t *buf)
{
  /* 설치된 펌웨어가 .version 섹션(=FLASH_ADDR_VER, m483-fw)에 심어둔 이름/버전. */
  const firm_ver_t *p_ver = (const firm_ver_t *)FLASH_ADDR_VER;
  int total = 0;
  int len;

  /* 부트로더 자신의 정보. */
  len = snprintf((char *)buf, VDISK_SECTOR_SIZE,
                 "%s UF2 Bootloader\r\n"
                 "Model : %s\r\n"
                 "Board-ID: %s\r\n"
                 "Version : %s\r\n\r\n",
                 UF2_PRODUCT_NAME, UF2_BOARD_ID, UF2_BOARD_ID, _DEF_FIRMWATRE_VERSION);
  if (len > 0)
    total = len;

  /* 설치된 펌웨어 정보(없거나 무효면 (none)). %.31s로 필드 밖 참조 방지. */
  if (p_ver->magic_number == VERSION_MAGIC_NUMBER)
  {
    len = snprintf((char *)buf + total, VDISK_SECTOR_SIZE - total,
                   "Firmware:\r\n"
                   "  Name : %.31s\r\n"
                   "  Ver  : %.31s\r\n",
                   p_ver->name_str, p_ver->version_str);
  }
  else
  {
    len = snprintf((char *)buf + total, VDISK_SECTOR_SIZE - total,
                   "Firmware: (none)\r\n");
  }
  if (len > 0)
    total += len;

  return (uint32_t)total;
}

static void build_boot_sector(uint8_t *b)
{
  b[0] = 0xEB; b[1] = 0x3C; b[2] = 0x90;
  memcpy(&b[3], "MSDOS5.0", 8);

  b[11] = 0x00; b[12] = 0x02;      /* 512 bytes/sector          */
  b[13] = 0x01;                    /* 1 sector/cluster          */
  b[14] = 0x01; b[15] = 0x00;      /* 1 reserved sector         */
  b[16] = 0x02;                    /* 2 FATs                    */
  b[17] = 0x00; b[18] = 0x02;      /* 512 root entries          */
  b[19] = 0x00; b[20] = 0x00;      /* TotSec16 = 0 -> use TotSec32 */
  b[21] = 0xF8;                    /* media                     */
  b[22] = 0x80; b[23] = 0x00;      /* 128 sectors/FAT           */
  b[24] = 0x01; b[25] = 0x00;      /* sectors/track             */
  b[26] = 0x01; b[27] = 0x00;      /* heads                     */
  b[28] = 0x00; b[29] = 0x00; b[30] = 0x00; b[31] = 0x00;   /* hidden */
  b[32] = 0x00; b[33] = 0x80; b[34] = 0x00; b[35] = 0x00;   /* TotSec32 = 32768 */

  b[36] = 0x80;                    /* drive number              */
  b[38] = 0x29;                    /* ext boot signature        */
  b[39] = 0x78; b[40] = 0x56; b[41] = 0x34; b[42] = 0x12;   /* volume serial */
  fat_label(&b[43]);               /* volume label (11)         */
  memcpy(&b[54], "FAT16   ", 8);   /* fs type                   */

  b[510] = 0x55; b[511] = 0xAA;
}

/* FAT sector 0 : cluster 0/1 reserved, cluster 2 = INFO_UF2.TXT (EOC). */
static void build_fat0(uint8_t *b)
{
  b[0] = 0xF8; b[1] = 0xFF;        /* cluster 0 : 0xFFF8 media   */
  b[2] = 0xFF; b[3] = 0xFF;        /* cluster 1 : 0xFFFF EOC     */
  b[4] = 0xFF; b[5] = 0xFF;        /* cluster 2 : 0xFFFF EOC     */
}

static void build_root_dir(uint8_t *b)
{
  static const char info_name[11] = { 'I','N','F','O','_','U','F','2','T','X','T' };
  uint8_t  tmp[VDISK_SECTOR_SIZE];
  uint32_t info_size = build_info_txt(tmp);

  /* entry 0 : volume label */
  fat_label(&b[0]);
  b[11] = 0x08;

  /* entry 1 : INFO_UF2.TXT (cluster 2) */
  memcpy(&b[32], info_name, 11);
  b[32 + 11] = 0x20;                                   /* attr = archive */
  b[32 + 26] = 0x02; b[32 + 27] = 0x00;                /* first cluster = 2 */
  b[32 + 28] = (uint8_t)(info_size & 0xFF);            /* file size */
  b[32 + 29] = (uint8_t)((info_size >> 8) & 0xFF);
  b[32 + 30] = (uint8_t)((info_size >> 16) & 0xFF);
  b[32 + 31] = (uint8_t)((info_size >> 24) & 0xFF);
}

void vdiskRead(uint32_t lba, uint8_t *buf512)
{
  memset(buf512, 0, VDISK_SECTOR_SIZE);

  if (lba == 0)
    build_boot_sector(buf512);
  else if (lba == VDISK_FAT1_LBA || lba == VDISK_FAT2_LBA)
    build_fat0(buf512);
  else if (lba == VDISK_ROOT_LBA)
    build_root_dir(buf512);
  else if (lba == VDISK_INFO_LBA)
    build_info_txt(buf512);
  /* 그 외 : 0 (이미 memset). */
}
