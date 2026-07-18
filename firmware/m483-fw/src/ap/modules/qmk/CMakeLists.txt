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

# 보드 옵션 (config.cmake): DEBOUNCE_RUNTIME, DEBOUNCE_TYPE 등.
if (EXISTS "${QMK_KEYBOARD_PATH}/config.cmake")
  include("${QMK_KEYBOARD_PATH}/config.cmake")
endif()

# 디바운스:
#  - DEBOUNCE_RUNTIME 켜면 런타임 TYPE/TIME 선택(VIA). stock 단일 디바운스는 컴파일하지 않고
#    port/debounce_runtime.c 가 debounce* 심볼을 제공하며 eager(GAMING)/defer(TYPING) 를 감싼다.
#  - 아니면 단일 알고리즘(DEBOUNCE_TYPE, 기본 sym_eager_pk).
if (DEBOUNCE_RUNTIME)
  message(STATUS "QMK DEBOUNCE = RUNTIME (GAMING=sym_eager_pk / TYPING=sym_defer_pk, VIA 선택)")
else()
  if (NOT DEFINED DEBOUNCE_TYPE OR DEBOUNCE_TYPE STREQUAL "")
    set(DEBOUNCE_TYPE sym_eager_pk)
  endif()
  message(STATUS "QMK DEBOUNCE_TYPE = ${DEBOUNCE_TYPE}")
endif()

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

  # VENOM VIA 커스텀 채널 (Phase B): 버전/시스템(DFU/EEPROM 리셋)
  ${QMK_ROOT_PATH}/port/ver_port.c
  ${QMK_ROOT_PATH}/port/sys_port.c

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
  ${QMK_ROOT_PATH}/quantum/process_keycode/process_key_override.c
  ${QMK_ROOT_PATH}/quantum/process_keycode/process_grave_esc.c
  ${QMK_ROOT_PATH}/quantum/process_keycode/process_magic.c   # NK_TOGG / GU_TOGG(Win락)
)

# 디바운스 소스 (런타임 vs 단일)
if (DEBOUNCE_RUNTIME)
  add_compile_definitions(DEBOUNCE_RUNTIME)
  list(APPEND QMK_SRC_FILES
    ${QMK_ROOT_PATH}/port/debounce_runtime.c
    ${QMK_ROOT_PATH}/port/debounce_eager_pk.c   # sym_eager_pk 래퍼
    ${QMK_ROOT_PATH}/port/debounce_defer_pk.c   # sym_defer_pk 래퍼
    ${QMK_ROOT_PATH}/port/debounce_cfg.c        # VIA 채널 + eeconfig
  )
else()
  list(APPEND QMK_SRC_FILES ${QMK_ROOT_PATH}/quantum/debounce/${DEBOUNCE_TYPE}.c)
endif()

# VENOM 게이밍 기능 (보드 config.cmake opt-in)
if (KILL_SWITCH_ENABLE)
  add_compile_definitions(KILL_SWITCH_ENABLE)
  list(APPEND QMK_SRC_FILES ${QMK_ROOT_PATH}/port/kill_switch.c)
endif()
if (KKUK_ENABLE)
  add_compile_definitions(KKUK_ENABLE)
  list(APPEND QMK_SRC_FILES ${QMK_ROOT_PATH}/port/kkuk.c)
endif()

# 보드별 port/*.c (via_port.c 등)
file(GLOB QMK_BOARD_PORT CONFIGURE_DEPENDS ${QMK_KEYBOARD_PATH}/port/*.c)
list(APPEND QMK_SRC_FILES ${QMK_BOARD_PORT})

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
