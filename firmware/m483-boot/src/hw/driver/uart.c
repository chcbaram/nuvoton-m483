/* 부트로더용 UART : UART1(PA8=RXD, PA9=TXD) TX 전용, 인터럽트 구동 링버퍼(비차단).
 * 로그가 USB BOT 처리(mscProcess)를 블로킹하지 않도록 uartWrite 는 링에 복사만 하고
 * 즉시 반환한다. 링이 가득 차면 드롭(로그보다 USB 타이밍 우선). */
#include "uart.h"

#ifdef _USE_HW_UART

#include <stdarg.h>
#include <stdio.h>

#define TX_BUF_SIZE   4096            /* power of two */
#define TX_BUF_MASK   (TX_BUF_SIZE - 1)

static volatile uint8_t  tx_buf[TX_BUF_SIZE];
static volatile uint32_t tx_head = 0;   /* write index (producer) */
static volatile uint32_t tx_tail = 0;   /* read index  (ISR)      */
static bool is_open = false;


void UART1_IRQHandler(void)
{
  if (UART_GET_INT_FLAG(UART1, UART_INTSTS_THREINT_Msk))
  {
    while (tx_tail != tx_head)
    {
      if (UART_IS_TX_FULL(UART1))
        break;
      UART_WRITE(UART1, tx_buf[tx_tail & TX_BUF_MASK]);
      tx_tail++;
    }

    if (tx_tail == tx_head)
      UART_DISABLE_INT(UART1, UART_INTEN_THREIEN_Msk);   /* 보낼 것 없으면 TX int off */
  }
}

bool uartInit(void)
{
  return true;
}

bool uartOpen(uint8_t ch, uint32_t baud)
{
  if (ch != _DEF_UART1)
    return false;

  CLK_SetModuleClock(UART1_MODULE, CLK_CLKSEL1_UART1SEL_PLL, CLK_CLKDIV0_UART1(1));
  CLK_EnableModuleClock(UART1_MODULE);

  SYS->GPA_MFPH &= ~(SYS_GPA_MFPH_PA8MFP_Msk | SYS_GPA_MFPH_PA9MFP_Msk);
  SYS->GPA_MFPH |= (SYS_GPA_MFPH_PA8MFP_UART1_RXD) | (SYS_GPA_MFPH_PA9MFP_UART1_TXD);

  UART_Open(UART1, baud);

  NVIC_SetPriority(UART1_IRQn, 6);     /* USB(USBD20)보다 낮게 */
  NVIC_EnableIRQ(UART1_IRQn);

  is_open = true;
  return true;
}

uint32_t uartWrite(uint8_t ch, uint8_t *p_data, uint32_t length)
{
  uint32_t i;

  if (!is_open || ch != _DEF_UART1)
    return 0;

  for (i = 0; i < length; i++)
  {
    if ((uint32_t)(tx_head - tx_tail) >= TX_BUF_SIZE)   /* 링 가득 -> 드롭 */
      break;
    tx_buf[tx_head & TX_BUF_MASK] = p_data[i];
    tx_head++;
  }

  UART_ENABLE_INT(UART1, UART_INTEN_THREIEN_Msk);       /* TX 드레인 시작 */
  return i;
}

uint32_t uartPrintf(uint8_t ch, const char *fmt, ...)
{
  char    buf[256];
  va_list args;
  int     len;

  va_start(args, fmt);
  len = vsnprintf(buf, sizeof(buf), fmt, args);
  va_end(args);

  if (len > 0)
    uartWrite(ch, (uint8_t *)buf, (uint32_t)len);

  return (len > 0) ? (uint32_t)len : 0;
}

bool     uartDeInit(void)          { return true; }
bool     uartIsInit(void)          { return is_open; }
bool     uartClose(uint8_t ch)     { (void)ch; return true; }
uint32_t uartAvailable(uint8_t ch) { (void)ch; return 0; }
bool     uartFlush(uint8_t ch)     { (void)ch; return true; }
uint8_t  uartRead(uint8_t ch)      { (void)ch; return 0; }
uint32_t uartGetBaud(uint8_t ch)   { (void)ch; return 115200; }
uint32_t uartGetRxCnt(uint8_t ch)  { (void)ch; return 0; }
uint32_t uartGetTxCnt(uint8_t ch)  { (void)ch; return 0; }

#endif
