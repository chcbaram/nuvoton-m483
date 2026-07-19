#include "ap.h"
#include "uf2/uf2.h"
#include "boot/boot.h"
#include "usb_msc/usbd_msc.h"   


static void bootUp(void);
static int  uf2Write(uint32_t lba, uint8_t *data);


void apInit(void)
{
  bootInit();
  bootUp();

  logPrintf("Mode      : BOOT (UF2 MSC)\r\n");
  uf2Init();
  usbInit();
  mscSetWriteCb(uf2Write);
}

void apMain(void)
{
  uint32_t pre_time    = millis();
  bool     eject_armed = false;
  bool     eject_done  = false;
  uint32_t eject_ms    = 0;

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

    /* 펌웨어 갱신 없이 USB 드라이브 "꺼내기" -> 부트 종료 후 앱 실행(baram/convex 참고).
     * 플래싱 중이면 무시(uf2Update 완료 경로가 스스로 리셋). CSW가 호스트로 전송될
     * 시간을 잠깐 준 뒤 bootJumpFirm() 직접 점프(부트키/조건 재평가 없이 앱으로). */
    if (!eject_done && !eject_armed && mscEjectRequested() && !uf2IsBusy())
    {
      eject_armed = true;
      eject_ms    = millis();
      logPrintf("Eject     : exit boot -> jump app\r\n");
    }
    if (eject_armed && (millis() - eject_ms >= 100))
    {
      bootJumpFirm();     /* 유효하면 리턴 안 함(앱으로 분기) */
      logPrintf("Eject     : app invalid -> stay boot\r\n");
      eject_armed = false;
      eject_done  = true; /* 무효 -> 재시도하지 않음(부트 잔류) */
    }
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

/* WRITE10 섹터 콜백(usb layer -> uf2). UF2 블록이면 플래시, 아니면(FAT/메타) 무시. */
static int uf2Write(uint32_t lba, uint8_t *data)
{
  (void)lba;
  return uf2_write_block(data);
}
