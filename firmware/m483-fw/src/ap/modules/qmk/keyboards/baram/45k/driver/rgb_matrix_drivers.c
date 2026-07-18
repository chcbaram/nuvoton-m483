/*
 * keyboards/baram/45k/driver/rgb_matrix_drivers.c
 *
 *  QMK rgb_matrix 커스텀 드라이버 백엔드 (M483 ws2812.c 로 연결) + g_led_config.
 *  stock quantum/rgb_matrix/rgb_matrix_drivers.c 는 빌드에서 제외하고 이 파일이
 *  rgb_matrix_driver 심볼을 제공한다. (qmk-zephyr baram/wish40 방식 이식)
 *
 *  M483 차이: ws2812SetPower/qmkIsSuspended 없음 -> flush 는 ws2812Refresh() 만.
 *  체인 45개(회로도) = HW_WS2812_MAX_CH. per-key RGB.
 */

#include "quantum.h"
#include "rgb_matrix.h"
#include "ws2812.h"

#if defined(RGB_MATRIX_ENABLE)

_Static_assert(RGB_MATRIX_LED_COUNT == HW_WS2812_MAX_CH,
               "RGB_MATRIX_LED_COUNT(config.h) != HW_WS2812_MAX_CH(hw_def.h)");


static void rgb_matrix_ws2812_init(void)
{
  /* ws2812Init() 는 hw.c(hwInit) 에서 이미 호출 */
}

static void rgb_matrix_ws2812_set_color(int index, uint8_t r, uint8_t g, uint8_t b)
{
  ws2812SetColor(index, WS2812_COLOR(r, g, b));
}

static void rgb_matrix_ws2812_set_color_all(uint8_t r, uint8_t g, uint8_t b)
{
  for (int i = 0; i < RGB_MATRIX_LED_COUNT; i++)
    ws2812SetColor(i, WS2812_COLOR(r, g, b));
}

static void rgb_matrix_ws2812_flush(void)
{
  ws2812Refresh();   /* 논블로킹: 이전 전송 중이면 skip, 아니면 PDMA 전송 시작 */
}

const rgb_matrix_driver_t rgb_matrix_driver = {
  .init          = rgb_matrix_ws2812_init,
  .flush         = rgb_matrix_ws2812_flush,
  .set_color     = rgb_matrix_ws2812_set_color,
  .set_color_all = rgb_matrix_ws2812_set_color_all,
};


/*
 * LED 물리 레이아웃. per-key(45키=45LED). row3 은 9키(col 4,5,8 은 스위치/LED 없음=NO_LED).
 * 체인 인덱스 순서/실제 좌표는 회로도 배선에 맞춰 추후 정밀화 가능(현재 2개만 실장).
 */
led_config_t g_led_config = {
  /* 키(row,col) -> LED 인덱스 */
  {
    {  0,      1,      2,      3,      4,      5,      6,      7,      8,      9,     10,     11 },
    { 12,     13,     14,     15,     16,     17,     18,     19,     20,     21,     22,     23 },
    { 24,     25,     26,     27,     28,     29,     30,     31,     32,     33,     34,     35 },
    { 36,     37,     38,     39, NO_LED, NO_LED,     40,     41, NO_LED,     42,     43,     44 },
  },
  /* LED 인덱스 -> 물리 좌표 (x:0~220, y:0~63) */
  {
    {   0,  0 }, {  20,  0 }, {  40,  0 }, {  60,  0 }, {  80,  0 }, { 100,  0 },
    { 120,  0 }, { 140,  0 }, { 160,  0 }, { 180,  0 }, { 200,  0 }, { 220,  0 },
    {   0, 21 }, {  20, 21 }, {  40, 21 }, {  60, 21 }, {  80, 21 }, { 100, 21 },
    { 120, 21 }, { 140, 21 }, { 160, 21 }, { 180, 21 }, { 200, 21 }, { 220, 21 },
    {   0, 42 }, {  20, 42 }, {  40, 42 }, {  60, 42 }, {  80, 42 }, { 100, 42 },
    { 120, 42 }, { 140, 42 }, { 160, 42 }, { 180, 42 }, { 200, 42 }, { 220, 42 },
    {   0, 63 }, {  20, 63 }, {  40, 63 }, {  60, 63 }, { 120, 63 }, { 140, 63 },
    { 180, 63 }, { 200, 63 }, { 220, 63 },
  },
  /* LED 인덱스 -> 플래그 (per-key) */
  {
    LED_FLAG_KEYLIGHT, LED_FLAG_KEYLIGHT, LED_FLAG_KEYLIGHT, LED_FLAG_KEYLIGHT, LED_FLAG_KEYLIGHT,
    LED_FLAG_KEYLIGHT, LED_FLAG_KEYLIGHT, LED_FLAG_KEYLIGHT, LED_FLAG_KEYLIGHT, LED_FLAG_KEYLIGHT,
    LED_FLAG_KEYLIGHT, LED_FLAG_KEYLIGHT, LED_FLAG_KEYLIGHT, LED_FLAG_KEYLIGHT, LED_FLAG_KEYLIGHT,
    LED_FLAG_KEYLIGHT, LED_FLAG_KEYLIGHT, LED_FLAG_KEYLIGHT, LED_FLAG_KEYLIGHT, LED_FLAG_KEYLIGHT,
    LED_FLAG_KEYLIGHT, LED_FLAG_KEYLIGHT, LED_FLAG_KEYLIGHT, LED_FLAG_KEYLIGHT, LED_FLAG_KEYLIGHT,
    LED_FLAG_KEYLIGHT, LED_FLAG_KEYLIGHT, LED_FLAG_KEYLIGHT, LED_FLAG_KEYLIGHT, LED_FLAG_KEYLIGHT,
    LED_FLAG_KEYLIGHT, LED_FLAG_KEYLIGHT, LED_FLAG_KEYLIGHT, LED_FLAG_KEYLIGHT, LED_FLAG_KEYLIGHT,
    LED_FLAG_KEYLIGHT, LED_FLAG_KEYLIGHT, LED_FLAG_KEYLIGHT, LED_FLAG_KEYLIGHT, LED_FLAG_KEYLIGHT,
    LED_FLAG_KEYLIGHT, LED_FLAG_KEYLIGHT, LED_FLAG_KEYLIGHT, LED_FLAG_KEYLIGHT, LED_FLAG_KEYLIGHT,
  },
};

#endif /* RGB_MATRIX_ENABLE */
