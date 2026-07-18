cmake_minimum_required(VERSION 3.13)


# 디바운스 알고리즘 (quantum/debounce/<name>.c)
#   sym_eager_pk        : 양쪽 에지 즉시 등록(누름/뗌 모두 최저지연) — 게이밍용.
#                         단, 채터는 하드웨어 스캔 읽기를 깨끗하게 해서 없애야 한다(디바운스로 못 가림).
#   asym_eager_defer_pk : 누름 즉시 + 뗌 지연(채터 필터) — 뗌 지연 때문에 게이밍엔 부적합.
set(DEBOUNCE_TYPE sym_eager_pk)
