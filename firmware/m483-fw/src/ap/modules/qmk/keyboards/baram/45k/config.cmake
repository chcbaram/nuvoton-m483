cmake_minimum_required(VERSION 3.13)


# 런타임 디바운스 (VIA 에서 TYPE/TIME 선택, eeconfig 영구저장)
#   TYPE: GAMING=sym_eager_pk(기본, 누름/뗌 즉시) / TYPING=sym_defer_pk(안정화 후)
#   TIME: 5~40ms
# 켜지 않으면 아래 DEBOUNCE_TYPE 단일 알고리즘으로 컴파일된다.
set(DEBOUNCE_RUNTIME true)

# set(DEBOUNCE_TYPE sym_eager_pk)   # DEBOUNCE_RUNTIME 미사용 시


# VENOM 게이밍 기능 (VIA 에서 설정, eeconfig 영구저장)
#   KILL_SWITCH : SOCD(상반키) 처리 - LR/UD 쌍에서 나중 입력 우선(반대키 해제)
#   KKUK(꾹)     : 2키 이상 홀드 시 터보 리피트
set(KILL_SWITCH_ENABLE true)
set(KKUK_ENABLE true)

# RGB Matrix (per-key SK6812/WS2812). 백엔드 = driver/rgb_matrix_drivers.c -> ws2812.c
set(RGB_MATRIX_ENABLE true)

# 웹 대시보드 텔레메트리 (raw HID 0xB0): INFO/LATENCY/MATRIX/LAYOUT + 점검(채터링/USB헬스).
# 점검은 평소 성능 영향 없음(웹에서 켤 때만 동작).
set(WEB_HID_ENABLE true)

#   HOLD_OKP : Hold On Other Key Press 를 VIA 에서 런타임 on/off (per-key 콜백)
set(HOLD_OKP_RUNTIME true)
