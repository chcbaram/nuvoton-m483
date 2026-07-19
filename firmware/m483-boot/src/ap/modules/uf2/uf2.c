/*
 * UF2 수신 -> 앱 영역 직접 기록 (baram의 UPDATE 스테이징 없음, M483 단일 플래시).
 * - 페이지 최초 진입 시에만 4K erase (비트맵 추적: 블록 순서 뒤섞임/재전송 대비).
 * - 앱 창([FLASH_ADDR_FIRM, FLASH_APP_LIMIT)) 밖 블록은 거부 -> 부트로더 자기보호.
 * - 완료 후 read-back CRC 검증 -> 태그를 마지막에 기록 (중단 시 유효 태그 없음 = 브릭 방지).
 * uf2_write_block()은 블로킹 FMC를 쓰므로 반드시 메인루프(소비자)에서 호출.
 */
#include "uf2.h"
#include "boot/boot.h"
#include "flash.h"
#include "util_core.h"
#include <string.h>


#define APP_PAGE_COUNT   ((FLASH_APROM_END - FLASH_ADDR_FIRM) / FLASH_PAGE_SIZE)

static WriteState uf2_state;
static uint8_t    page_erased[(APP_PAGE_COUNT + 7) / 8];
static uint32_t   fw_max_end;
static bool       session_active;
static bool       write_complete;
static bool       write_error;
static bool       finalized;
static uint32_t   last_activity_ms;
static uint32_t   finalized_ms;


static inline bool is_uf2_block(UF2_Block const *bl)
{
  return (bl->magicStart0 == UF2_MAGIC_START0) &&
         (bl->magicStart1 == UF2_MAGIC_START1) &&
         (bl->magicEnd    == UF2_MAGIC_END)    &&
         (bl->flags & UF2_FLAG_FAMILYID)       &&
         !(bl->flags & UF2_FLAG_NOFLASH);
}

static void ensure_page_erased(uint32_t addr)
{
  if (addr < FLASH_ADDR_FIRM || addr >= FLASH_APROM_END)
    return;

  uint32_t idx  = (addr - FLASH_ADDR_FIRM) / FLASH_PAGE_SIZE;
  uint8_t  mask = 1u << (idx & 7u);

  if (page_erased[idx >> 3] & mask)
    return;

  flashErase(addr & ~(FLASH_PAGE_SIZE - 1u), FLASH_PAGE_SIZE);
  page_erased[idx >> 3] |= mask;
}

static void session_begin(void)
{
  memset(&uf2_state, 0, sizeof(uf2_state));
  memset(page_erased, 0, sizeof(page_erased));
  fw_max_end     = FLASH_ADDR_FIRM;
  write_complete = false;
  write_error    = false;
  finalized      = false;

  /* 플래싱 시작 시 태그 페이지를 지워 앱을 무효화 -> 도중 리셋돼도 반쪽 이미지로 부팅 안 함. */
  flashErase(FLASH_ADDR_TAG, FLASH_PAGE_SIZE);
  session_active = true;
}

static void finalize(void)
{
  uint32_t fw_size = fw_max_end - FLASH_ADDR_FIRM;
  /* 기록된 플래시를 주소 순서로 read-back 하여 CRC 산출(쓰기 순서와 무관하게 결정적). */
  uint16_t crc     = utilCalcCRC(0, (uint8_t *)FLASH_ADDR_FIRM, fw_size);

  /* 쓰기 실패(갭)면 태그를 안 써서 앱을 무효로 둔다 -> 부트 잔류 -> 재플래시 유도. */
  if (!write_error)
    bootWriteTag(fw_size, crc);

  finalized    = true;
  finalized_ms = millis();
}


void uf2Init(void)
{
  memset(&uf2_state, 0, sizeof(uf2_state));
  memset(page_erased, 0, sizeof(page_erased));
  fw_max_end       = FLASH_ADDR_FIRM;
  session_active   = false;
  write_complete   = false;
  finalized        = false;
  last_activity_ms = 0;
}

bool uf2IsBusy(void)
{
  return session_active && !finalized;
}

int uf2_write_block(uint8_t *data)
{
  UF2_Block *bl = (UF2_Block *)(void *)data;

  if (!is_uf2_block(bl) || bl->familyID != BOARD_UF2_FAMILY_ID)
    return -1;   /* UF2 아님(FAT 메타데이터) 또는 다른 칩/펌웨어 -> 무시 */

  uint32_t addr = bl->targetAddr;
  uint32_t len  = bl->payloadSize;

  /* 앱 창 밖/오버플로 블록 거부 (부트로더 영역 덮어쓰기 = 브릭 방지). */
  if (len == 0 || len > sizeof(bl->data) ||
      addr < FLASH_ADDR_FIRM ||
      (addr + len) > FLASH_APP_LIMIT ||
      (addr + len) < addr)
  {
    return 0;
  }

  if (!session_active)
    session_begin();

  last_activity_ms = millis();

  ensure_page_erased(addr);
  ensure_page_erased(addr + len - 1u);

  if (!flashWrite(addr, bl->data, len))
    write_error = true;                 /* read-back 불일치 -> 태그 미기록 */

  if ((addr + len) > fw_max_end)
    fw_max_end = addr + len;

  /* 완료 감지 : writtenMask 로 distinct 블록 수를 세어 numBlocks 도달 시 완료(순서 무관). */
  if (bl->numBlocks)
  {
    if (uf2_state.numBlocks != bl->numBlocks)
    {
      /* numBlocks 불일치(다중 이미지/손상) -> 0xFFFFFFFF 로 두면 idle 타임아웃 경로로 넘어감. */
      if (bl->numBlocks >= MAX_BLOCKS || uf2_state.numBlocks)
        uf2_state.numBlocks = 0xFFFFFFFFUL;
      else
        uf2_state.numBlocks = bl->numBlocks;
    }

    if (bl->blockNo < MAX_BLOCKS)
    {
      uint8_t  mask = 1u << (bl->blockNo & 7u);
      uint32_t pos  = bl->blockNo >> 3;

      if (!(uf2_state.writtenMask[pos] & mask))
      {
        uf2_state.writtenMask[pos] |= mask;
        uf2_state.numWritten++;
      }

      if (uf2_state.numWritten >= uf2_state.numBlocks)
        write_complete = true;
    }
  }

  return 1;
}

void uf2Update(void)
{
  /* 완료 후 잠깐 대기 -> 소프트 리셋 (부트 결정을 다시 타서 유효해진 앱으로 점프). */
  if (finalized)
  {
    if (millis() - finalized_ms >= 300)
      NVIC_SystemReset();
    return;
  }

  if (!session_active)
    return;

  /* eject(START/STOP)에 의존하지 않음(일부 OS 미전송) -> numBlocks 도달 or idle 타임아웃. */
  bool idle = (millis() - last_activity_ms) > 500;

  if (write_complete || (idle && uf2_state.numWritten > 0))
    finalize();
}
