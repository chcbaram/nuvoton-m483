/*
 * prof.h — DWT(CYCCNT) 사이클 프로파일러.
 *
 *  Cortex-M4 의 DWT 사이클 카운터로 코드 구간을 사이클 단위로 계측한다.
 *  (192MHz -> 1cyc ≈ 5.2ns, 오버헤드 수 사이클)
 *
 *  사용:
 *    uint32_t c0 = profNow();
 *    ... 측정할 코드 ...
 *    profAdd(PROF_SEND, "send", profNow() - c0);
 *  덤프: cliQmk 의 "qmk prof" (avg/max 사이클 & us), "qmk prof reset".
 */
#pragma once

#include <stdint.h>

enum
{
  PROF_KBD_TASK = 0,   /* keyboard_task 전체 (변화 스캔만) */
  PROF_MATRIX,         /* matrix_scan */
  PROF_ACTION,         /* action_exec (process_record 포함) */
  PROF_HOOK,           /* process_record_user (SOCD/kkuk) */
  PROF_SEND,           /* USB send + enqueue */
  PROF_MAX
};

void     profInit(void);                                   /* DWT 카운터 enable */
uint32_t profNow(void);                                    /* 현재 사이클 */
void     profAdd(uint8_t id, const char *name, uint32_t cyc);
void     profReset(void);
void     profDump(void);                                   /* cliPrintf 로 표 출력 */
