/*
 * usbd_msc_disk.h
 *
 *  읽기전용 FAT16 이미지(READ10). 레이아웃은 convex-boot(검증된 부트로더)와 동일:
 *  16MB / 32768 섹터 / 512 root entries / cluster 1 / FAT 2개(128섹터).
 *    LBA 0        : boot sector
 *    LBA 1..128   : FAT1
 *    LBA 129..256 : FAT2
 *    LBA 257..288 : root directory (32섹터)
 *    LBA 289      : cluster 2 = INFO_UF2.TXT
 */
#ifndef SRC_HW_DRIVER_USB_USB_MSC_USBD_MSC_DISK_H_
#define SRC_HW_DRIVER_USB_USB_MSC_USBD_MSC_DISK_H_

#ifdef __cplusplus
extern "C" {
#endif

#include "hw_def.h"

#define VDISK_SECTOR_SIZE         512
#define VDISK_TOTAL_SECTORS       32768         /* 16MB, FAT16 */

#define VDISK_FAT1_LBA            1
#define VDISK_FAT2_LBA            129
#define VDISK_ROOT_LBA            257
#define VDISK_INFO_LBA            289           /* cluster 2 */

void vdiskRead(uint32_t lba, uint8_t *buf512);

#ifdef __cplusplus
}
#endif

#endif /* SRC_HW_DRIVER_USB_USB_MSC_USBD_MSC_DISK_H_ */
