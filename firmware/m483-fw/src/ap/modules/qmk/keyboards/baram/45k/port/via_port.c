/*
 * keyboards/baram/45k/port/via_port.c
 *
 *  VIA 커스텀 채널 디스패치 (VENOM 스타일). via.c 의 weak via_custom_value_command_kb 를
 *  오버라이드하여 channel_id(data[1]) 로 각 핸들러에 전달한다.
 *   - id_qmk_version(8)  : 펌웨어 버전 표시            -> ver_port.c
 *   - id_qmk_system(9)   : DFU 진입 / EEPROM 리셋       -> sys_port.c
 *   - id_qmk_debounce(13): 런타임 디바운스 TYPE/TIME    -> debounce_cfg.c (DEBOUNCE_RUNTIME)
 *  (kill_switch/kkuk/hold_okp/led_caps 는 Phase B 후속)
 */

#include "quantum.h"
#include "via.h"
#include "ver_port.h"
#include "sys_port.h"
#ifdef DEBOUNCE_RUNTIME
#include "debounce_cfg.h"
#endif
#ifdef KILL_SWITCH_ENABLE
#include "kill_switch.h"
#endif
#ifdef KKUK_ENABLE
#include "kkuk.h"
#endif
#ifdef HOLD_OKP_RUNTIME
#include "hold_okp.h"
#endif


#ifdef NKRO_ENABLE
/* NKRO 토글 (QMK 네이티브 keymap_config.nkro). 별도 eeconfig 불필요 - keymap 에 저장됨. */
enum via_qmk_nkro_value {
    id_qmk_nkro_enable = 1,
};

static void via_qmk_nkro_command(uint8_t *data, uint8_t length)
{
  uint8_t *command_id = &(data[0]);
  uint8_t *value_id   = &(data[2]);
  uint8_t *value_data = &(data[3]);

  switch (*command_id)
  {
    case id_custom_set_value:
      if (*value_id == id_qmk_nkro_enable)
      {
        keymap_config.nkro = value_data[0] ? 1 : 0;
        eeconfig_update_keymap(keymap_config.raw);
        clear_keyboard();   /* 모드 전환 시 눌린 키 잔상 제거 */
      }
      break;

    case id_custom_get_value:
      if (*value_id == id_qmk_nkro_enable)
        value_data[0] = keymap_config.nkro;
      break;

    case id_custom_save:
      /* set 시 이미 eeconfig 반영됨 */
      break;

    default:
      *command_id = id_unhandled;
      break;
  }
}
#endif


void via_custom_value_command_kb(uint8_t *data, uint8_t length)
{
  // data = [ command_id, channel_id, value_id, value_data ]
  uint8_t *command_id = &(data[0]);
  uint8_t *channel_id = &(data[1]);

  if (*channel_id == id_qmk_version)
  {
    via_qmk_version(data, length);
    return;
  }

  if (*channel_id == id_qmk_system)
  {
    via_qmk_system(data, length);
    return;
  }

#ifdef DEBOUNCE_RUNTIME
  if (*channel_id == id_qmk_debounce)
  {
    via_qmk_debounce_command(data, length);
    return;
  }
#endif

#ifdef KILL_SWITCH_ENABLE
  if (*channel_id == id_qmk_kill_switch_lr)
  {
    via_qmk_kill_swtich_command(0, data, length);   /* LR */
    return;
  }
  if (*channel_id == id_qmk_kill_switch_ud)
  {
    via_qmk_kill_swtich_command(1, data, length);   /* UD */
    return;
  }
#endif

#ifdef KKUK_ENABLE
  if (*channel_id == id_qmk_kkuk)
  {
    via_qmk_kkuk_command(data, length);
    return;
  }
#endif

#ifdef HOLD_OKP_RUNTIME
  if (*channel_id == id_qmk_hold_okp)
  {
    via_qmk_hold_okp_command(data, length);
    return;
  }
#endif

#ifdef NKRO_ENABLE
  if (*channel_id == id_qmk_nkro)
  {
    via_qmk_nkro_command(data, length);
    return;
  }
#endif

  // 미처리 -> unhandled
  *command_id = id_unhandled;
}
