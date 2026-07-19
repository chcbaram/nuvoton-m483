#include "ap.h"
#include "uf2/uf2.h"
#include "boot/boot.h"


static void bootUp(void);


/* WRITE10 섹터 콜백(usb layer -> uf2). UF2 블록이면 플래시, 아니면(FAT/메타) 무시. */
static int apUf2Write(uint32_t lba, uint8_t *data)
{
  (void)lba;
  return uf2_write_block(data);
}

void apInit(void)
{
  bootInit();
  bootUp();

  logPrintf("Mode      : BOOT (UF2 MSC)\r\n");
  uf2Init();
  usbInit();
  mscSetWriteCb(apUf2Write);
}

void apMain(void)
{
  uint32_t pre_time = millis();

  while (1)
  {
    /* 플래싱 중엔 LED 빠르게 깜빡여 부트모드 표시. */
    if (millis() - pre_time >= (uf2IsBusy() ? 80 : 400))
    {
      pre_time = millis();
      ledToggle(_DEF_LED1);
    }

    usbUpdate();
    uf2Update();
  }
}

/* 부트모드 플래그 or 부트키 홀드면 부트 잔류, 아니면 앱 점프.
 * bootJumpFirm()은 앱이 유효할 때만 점프(무효면 리턴) -> 그대로 부트 루프로 진입. */
static void bootUp(void)
{
  bool run_fw = true;

  if (resetGetBootMode() & (1 << MODE_BIT_BOOT))
  {
    logPrintf("Enter     : boot flag\r\n");
    run_fw = false;
  }

  /* 부트키는 창 전체에서 지속적으로 눌려 있어야 인정(단일 글리치 오검출 방지). */
  uint32_t pre_time = millis();
  uint32_t samples  = 0;
  uint32_t hits     = 0;
  while (millis() - pre_time <= BOOT_KEY_SETTLE_MS)
  {
    keysUpdate();
    samples++;
    if (keysGetPressed(BOOT_KEY_ROW, BOOT_KEY_COL))
      hits++;
  }
  if (samples > 0 && hits == samples)
  {
    logPrintf("Enter     : boot key\r\n");
    run_fw = false;
  }

  if (run_fw)
  {
    if (bootIsFirmValid())
    {
      logPrintf("App valid : jump 0x%08X\r\n", (unsigned)FLASH_ADDR_FIRM);

      /* 앱 진입 전 LED 짧게 깜빡. */
      for (int i = 0; i < 6; i++)
      {
        ledToggle(_DEF_LED1);
        delay(40);
      }

      bootJumpFirm();     /* 유효하면 점프(리턴 안 함), 아니면 아래로 */
    }
    logPrintf("App       : invalid -> stay\r\n");
  }
}
