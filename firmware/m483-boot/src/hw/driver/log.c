/* 부트로더용 최소 로그 : uart 로 바로 출력(링버퍼/CLI 미사용). */
#include "log.h"

#ifdef _USE_HW_LOG

#include "uart.h"
#include <stdarg.h>
#include <stdio.h>

static bool     is_open = false;
static uint8_t  log_ch  = LOG_CH;
static char     print_buf[256];


bool logInit(void)
{
  return true;
}

bool logOpen(uint8_t ch, uint32_t baud)
{
  log_ch  = ch;
  is_open = uartOpen(ch, baud);
  return is_open;
}

void logPrintf(const char *fmt, ...)
{
  va_list args;
  int     len;

  if (!is_open)
    return;

  va_start(args, fmt);
  len = vsnprintf(print_buf, sizeof(print_buf), fmt, args);
  va_end(args);

  if (len > 0)
    uartWrite(log_ch, (uint8_t *)print_buf, (uint32_t)len);
}

void logEnable(void)              { }
void logDisable(void)             { }
void logBoot(uint8_t enable)      { (void)enable; }

#endif
