cmake_minimum_required(VERSION 3.13)

# QMK 통합 (Phase A: 코어 타이핑 + 동적 NKRO + 기본 VIA).
# 전송은 QMK 네이티브 host_driver_t(port/driver_usb.c) 방식.
# VENOM(chattering/kill_switch/kkuk/hold_okp/sys_port/ver_port/web_hid/debounce_cfg 등)은
# Phase B 로 분리 — 여기서 컴파일하지 않는다(파일은 폴더에 유지).

set(QMK_ROOT_PATH "${CMAKE_CURRENT_SOURCE_DIR}/src/ap/modules/qmk")

# 보드 선택: -DKEYBOARD_PATH='/keyboards/baram/45k' (기본 45k)
if (NOT DEFINED KEYBOARD_PATH OR KEYBOARD_PATH STREQUAL "")
  set(KEYBOARD_PATH "/keyboards/baram/45k")
endif()
set(QMK_KEYBOARD_PATH "${QMK_ROOT_PATH}${KEYBOARD_PATH}")

if (NOT EXISTS "${QMK_KEYBOARD_PATH}/config.h")
  message(FATAL_ERROR "KEYBOARD_PATH invalid: ${QMK_KEYBOARD_PATH}/config.h not found")
endif()
message(STATUS "QMK KEYBOARD_PATH = ${KEYBOARD_PATH}")

# ---- 컴파일할 소스 (명시적 allow-list) ----
set(QMK_SRC_FILES
  ${QMK_ROOT_PATH}/qmk.c

  # port glue (코어만)
  ${QMK_ROOT_PATH}/port/matrix.c
  ${QMK_ROOT_PATH}/port/via_hid.c
  ${QMK_ROOT_PATH}/port/driver_usb.c
  ${QMK_ROOT_PATH}/port/protocol/host.c
  ${QMK_ROOT_PATH}/port/protocol/report.c
  ${QMK_ROOT_PATH}/port/platforms/eeprom.c
  ${QMK_ROOT_PATH}/port/platforms/timer.c
  ${QMK_ROOT_PATH}/port/platforms/wait.c
  ${QMK_ROOT_PATH}/port/platforms/bootloader.c
  ${QMK_ROOT_PATH}/port/platforms/suspend.c

  # quantum (stock, 최소 집합)
  ${QMK_ROOT_PATH}/quantum/quantum.c
  ${QMK_ROOT_PATH}/quantum/via.c
  ${QMK_ROOT_PATH}/quantum/keyboard.c
  ${QMK_ROOT_PATH}/quantum/action.c
  ${QMK_ROOT_PATH}/quantum/action_tapping.c
  ${QMK_ROOT_PATH}/quantum/action_util.c
  ${QMK_ROOT_PATH}/quantum/action_layer.c
  ${QMK_ROOT_PATH}/quantum/keycode_config.c
  ${QMK_ROOT_PATH}/quantum/led.c
  ${QMK_ROOT_PATH}/quantum/keymap_common.c
  ${QMK_ROOT_PATH}/quantum/dynamic_keymap.c
  ${QMK_ROOT_PATH}/quantum/eeconfig.c
  ${QMK_ROOT_PATH}/quantum/keymap_introspection.c   # #include KEYMAP_C (keymap.c 직접 컴파일 안함)
  ${QMK_ROOT_PATH}/quantum/color.c
  ${QMK_ROOT_PATH}/quantum/mousekey.c
  ${QMK_ROOT_PATH}/quantum/debounce/sym_eager_pk.c
  ${QMK_ROOT_PATH}/quantum/process_keycode/process_key_override.c
  ${QMK_ROOT_PATH}/quantum/process_keycode/process_grave_esc.c
  ${QMK_ROOT_PATH}/quantum/process_keycode/process_magic.c   # NK_TOGG / GU_TOGG(Win락)
)

file(GLOB QMK_SRC_GLOB CONFIGURE_DEPENDS
  ${QMK_ROOT_PATH}/quantum/sequencer/*.c
  ${QMK_ROOT_PATH}/quantum/logging/*.c
  ${QMK_ROOT_PATH}/quantum/send_string/*.c
)
list(APPEND QMK_SRC_FILES ${QMK_SRC_GLOB})

# ---- include 경로 ----
set(QMK_INC_DIR
  ${QMK_ROOT_PATH}
  ${QMK_ROOT_PATH}/port
  ${QMK_ROOT_PATH}/port/platforms
  ${QMK_ROOT_PATH}/port/protocol
  ${QMK_ROOT_PATH}/quantum
  ${QMK_ROOT_PATH}/quantum/logging
  ${QMK_ROOT_PATH}/quantum/keymap_extras
  ${QMK_ROOT_PATH}/quantum/sequencer
  ${QMK_ROOT_PATH}/quantum/send_string
  ${QMK_ROOT_PATH}/quantum/process_keycode
  ${QMK_KEYBOARD_PATH}
)

# ---- feature 정의 (핵심 진입점 3종은 top-level 에서 add_compile_definitions) ----
set(QMK_KEYMAP_CONFIG_H_PATH "${QMK_KEYBOARD_PATH}/config.h")
set(QMK_KEYMAP_C_PATH        "${QMK_KEYBOARD_PATH}/keymap.c")
