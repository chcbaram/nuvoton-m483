/*
 * prof.c — DWT 사이클 프로파일러 구현. (prof.h 참조)
 */

#include "prof.h"
#include "hw_def.h"     /* NuMicro -> DWT / CoreDebug / SystemCoreClock */
#include "cli.h"
#include <string.h>


typedef struct
{
  const char *name;
  uint32_t    sum;
  uint32_t    cnt;
  uint32_t    max;
} prof_bin_t;

static prof_bin_t bins[PROF_MAX];


void profInit(void)
{
  CoreDebug->DEMCR |= CoreDebug_DEMCR_TRCENA_Msk;   /* trace enable */
  DWT->CYCCNT = 0;
  DWT->CTRL  |= DWT_CTRL_CYCCNTENA_Msk;             /* 사이클 카운터 on */
  profReset();
}

uint32_t profNow(void)
{
  return DWT->CYCCNT;
}

void profAdd(uint8_t id, const char *name, uint32_t cyc)
{
  if (id >= PROF_MAX)
    return;
  bins[id].name = name;
  bins[id].sum += cyc;
  bins[id].cnt++;
  if (cyc > bins[id].max)
    bins[id].max = cyc;
}

void profReset(void)
{
  for (uint8_t i = 0; i < PROF_MAX; i++)
  {
    bins[i].sum = 0;
    bins[i].cnt = 0;
    bins[i].max = 0;
  }
}

void profDump(void)
{
  uint32_t mhz = SystemCoreClock / 1000000U;   /* 192 */

  cliPrintf("%-12s %10s %10s %8s  %8s\n", "section", "avg(cyc)", "max(cyc)", "avg(us)", "max(us)");
  for (uint8_t i = 0; i < PROF_MAX; i++)
  {
    uint32_t avg;

    if (bins[i].cnt == 0)
      continue;

    avg = bins[i].sum / bins[i].cnt;
    cliPrintf("%-12s %10d %10d  %3d.%02d   %3d.%02d   (n=%d)\n",
              bins[i].name ? bins[i].name : "?",
              (int)avg, (int)bins[i].max,
              (int)(avg / mhz), (int)((avg % mhz) * 100 / mhz),
              (int)(bins[i].max / mhz), (int)((bins[i].max % mhz) * 100 / mhz),
              (int)bins[i].cnt);
  }
}
