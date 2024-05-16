#include "uart.h"

#ifdef _USE_HW_UART
#include "qbuffer.h"
#include "cli.h"
#ifdef _USE_HW_USB
#include "cdc.h"
#endif


#define UART_RX_BUF_LENGTH        1024


typedef struct
{
  const char *p_msg;
  UART_T     *p_uart;
  uint32_t    dma_rx_ch;
} uart_hw_t;


typedef struct
{
  bool     is_open;
  uint32_t baud;

  uint8_t   rx_buf[UART_RX_BUF_LENGTH];
  qbuffer_t qbuffer;
  uart_hw_t *p_hw;

  uint32_t rx_cnt;
  uint32_t tx_cnt;
} uart_tbl_t;





#ifdef _USE_HW_CLI
static void cliUart(cli_args_t *args);
#endif
static bool uartInitHw(uint8_t ch);


static bool is_init = false;

__attribute__((section(".non_cache")))
static uart_tbl_t uart_tbl[UART_MAX_CH];



const static uart_hw_t uart_hw_tbl[UART_MAX_CH] = 
  {
    {"USART1 SWD   ", UART1, 0},
  };


void uartTest(void)
{
  uartPrintf(_DEF_UART1, "txd %X, %X\n", PDMA->DSCT[0].CTL, uart_tbl[0].rx_buf[0]);
}


bool uartInit(void)
{
  for (int i=0; i<UART_MAX_CH; i++)
  {
    uart_tbl[i].is_open = false;
    uart_tbl[i].baud = 57600;
    uart_tbl[i].rx_cnt = 0;
    uart_tbl[i].tx_cnt = 0;    
  }

  is_init = true;

#ifdef _USE_HW_CLI
  cliAdd("uart", cliUart);
#endif
  return true;
}

bool uartDeInit(void)
{
  return true;
}

bool uartIsInit(void)
{
  return is_init;
}

bool uartOpen(uint8_t ch, uint32_t baud)
{
  bool ret = false;


  if (ch >= UART_MAX_CH) return false;

  if (uart_tbl[ch].is_open == true && uart_tbl[ch].baud == baud)
  {
    return true;
  }


  switch(ch)
  {
    case _DEF_UART1:
      uart_tbl[ch].baud = baud;

      uart_tbl[ch].p_hw = (uart_hw_t *)&uart_hw_tbl[ch];

      uartInitHw(ch);

      UART_SetLineConfig(uart_tbl[ch].p_hw->p_uart,
                         baud,
                         UART_WORD_LEN_8,
                         UART_PARITY_NONE,
                         UART_STOP_BIT_1);
      UART_Open(uart_tbl[ch].p_hw->p_uart, baud);

      qbufferCreate(&uart_tbl[ch].qbuffer, &uart_tbl[ch].rx_buf[0], UART_RX_BUF_LENGTH);


      ret = true;
      uart_tbl[ch].is_open = true;


      // uart_tbl[ch].qbuffer.in  = uart_tbl[ch].qbuffer.len - uart_tbl[ch].p_hw->p_hdma_rx->CHNDATA_B.NDATA;
      uart_tbl[ch].qbuffer.out = uart_tbl[ch].qbuffer.in;
      break;

    case _DEF_UART2:
      uart_tbl[ch].baud    = baud;
      uart_tbl[ch].is_open = true;
      ret = true;
      break;      
  }

  return ret;
}

bool uartClose(uint8_t ch)
{
  if (ch >= UART_MAX_CH) return false;

  uart_tbl[ch].is_open = false;

  return true;
}

bool uartInitHw(uint8_t ch)
{
  bool ret = true;
  uart_hw_t *p_hw = uart_tbl[ch].p_hw;


  switch(ch)
  {
    case _DEF_UART1:

      /* Select IP clock source */
      CLK_SetModuleClock(UART1_MODULE, CLK_CLKSEL1_UART1SEL_PLL, CLK_CLKDIV0_UART1(1));

      /* Enable IP clock */
      CLK_EnableModuleClock(UART1_MODULE);
      CLK_EnableModuleClock(PDMA_MODULE);

      /* Set PA multi-function pins for UART1 TXD, RXD */
      SYS->GPA_MFPH &= ~(SYS_GPA_MFPH_PA8MFP_Msk | SYS_GPA_MFPH_PA9MFP_Msk );
      SYS->GPA_MFPH |= (SYS_GPA_MFPH_PA8MFP_UART1_RXD) | (SYS_GPA_MFPH_PA9MFP_UART1_TXD);

      #if 0
      /* Open PDMA Channel */
      PDMA_Open(PDMA, 1 << p_hw->dma_rx_ch); // Channel 0 for UART1 RX
      // Select basic mode
      PDMA_SetTransferMode(PDMA, p_hw->dma_rx_ch, PDMA_UART1_RX, 0, 0);
      // Set data width and transfer count
      PDMA_SetTransferCnt(PDMA, p_hw->dma_rx_ch, PDMA_WIDTH_8, UART_RX_BUF_LENGTH);
      // Set PDMA Transfer Address
      PDMA_SetTransferAddr(PDMA, p_hw->dma_rx_ch, UART1_BASE, PDMA_SAR_FIX, ((uint32_t)(&uart_tbl[ch].rx_buf[0])), PDMA_DAR_INC);
      // Select Single Request
      PDMA_SetBurstType(PDMA, p_hw->dma_rx_ch, PDMA_REQ_SINGLE, 0);
      #endif

      p_hw->p_uart->FIFO &= ~ UART_FIFO_RFITL_Msk;
      p_hw->p_uart->FIFO |= UART_FIFO_RFITL_1BYTE;
      
      NVIC_SetPriority(UART1_IRQn, 5);
      NVIC_EnableIRQ(UART1_IRQn);
      UART_EnableInt(UART1, (UART_INTEN_RDAIEN_Msk));
      break;

    default:
      ret = false;
      break;
  }

  return ret;
}


uint32_t uartAvailable(uint8_t ch)
{
  uint32_t ret = 0;


  switch(ch)
  {
    case _DEF_UART1:
      // uart_tbl[ch].qbuffer.in = uart_tbl[ch].qbuffer.len - uart_tbl[ch].p_hw->p_hdma_rx->CHNDATA_B.NDATA;
      ret = qbufferAvailable(&uart_tbl[ch].qbuffer);      
      break;

    case _DEF_UART2:
      #ifdef _USE_HW_USB
      ret = cdcAvailable();
      #endif
      break;      
  }

  return ret;
}

bool uartFlush(uint8_t ch)
{
  uint32_t pre_time;


  pre_time = millis();
  while(uartAvailable(ch))
  {
    if (millis()-pre_time >= 10)
    {
      break;
    }
    uartRead(ch);
  }

  return true;
}

uint8_t uartRead(uint8_t ch)
{
  uint8_t ret = 0;


  switch(ch)
  {
    case _DEF_UART1:
      qbufferRead(&uart_tbl[ch].qbuffer, &ret, 1);
      break;

    case _DEF_UART2:
      #ifdef _USE_HW_USB
      ret = cdcRead();
      #endif
      break;      
  }
  uart_tbl[ch].rx_cnt++;

  return ret;
}

uint32_t uartWrite(uint8_t ch, uint8_t *p_data, uint32_t length)
{
  uint32_t ret = 0;
  uint32_t pre_time;
  uint32_t index;


  pre_time = millis();
  switch(ch)
  {
    case _DEF_UART1:
      index = 0;
      while(millis()-pre_time < 100)
      {
        if (!UART_IS_TX_FULL(uart_tbl[ch].p_hw->p_uart))
        {
          UART_WRITE(uart_tbl[ch].p_hw->p_uart, p_data[index]);
          index++;
          if (index >= length)
          {
            ret = index;
            break;
          }
        }
      }
      break;

    case _DEF_UART2:
      #ifdef _USE_HW_USB
      ret = cdcWrite(p_data, length);
      #endif
      break;      
  }
  uart_tbl[ch].tx_cnt += ret;

  return ret;
}

uint32_t uartPrintf(uint8_t ch, const char *fmt, ...)
{
  char buf[256];
  va_list args;
  int len;
  uint32_t ret;

  va_start(args, fmt);
  len = vsnprintf(buf, 256, fmt, args);

  ret = uartWrite(ch, (uint8_t *)buf, len);

  va_end(args);


  return ret;
}

uint32_t uartGetBaud(uint8_t ch)
{
  uint32_t ret = 0;


  if (ch >= UART_MAX_CH) return 0;

  #ifdef _USE_HW_USB
  if (ch == HW_UART_CH_USB)
    ret = cdcGetBaud();
  else
    ret = uart_tbl[ch].baud;
  #else
  ret = uart_tbl[ch].baud;
  #endif
  
  return ret;
}

uint32_t uartGetRxCnt(uint8_t ch)
{
  if (ch >= UART_MAX_CH) return 0;

  return uart_tbl[ch].rx_cnt;
}

uint32_t uartGetTxCnt(uint8_t ch)
{
  if (ch >= UART_MAX_CH) return 0;

  return uart_tbl[ch].tx_cnt;
}

void UART1_IRQHandler(void)
{
  volatile uint32_t int_status = UART1->INTSTS;


  if((int_status & UART_INTSTS_RDAINT_Msk))
  {
    uint8_t rxd;

    while(UART_GET_RX_EMPTY(UART1) == 0)
    {
      rxd = UART_READ(UART1);
      qbufferWrite(&uart_tbl[_DEF_UART1].qbuffer, &rxd, 1);        
    }    
  }
  return;
}

#ifdef _USE_HW_CLI
void cliUart(cli_args_t *args)
{
  bool ret = false;


  if (args->argc == 1 && args->isStr(0, "info"))
  {
    for (int i=0; i<UART_MAX_CH; i++)
    {
      cliPrintf("_DEF_UART%d : %s, %d bps\n", i+1, uart_hw_tbl[i].p_msg, uartGetBaud(i));
    }
    ret = true;
  }

  if (args->argc == 2 && args->isStr(0, "test"))
  {
    uint8_t uart_ch;

    uart_ch = constrain(args->getData(1), 1, UART_MAX_CH) - 1;

    if (uart_ch != cliGetPort())
    {
      uint8_t rx_data;

      while(1)
      {
        if (uartAvailable(uart_ch) > 0)
        {
          rx_data = uartRead(uart_ch);
          cliPrintf("<- _DEF_UART%d RX : 0x%X\n", uart_ch + 1, rx_data);
        }

        if (cliAvailable() > 0)
        {
          rx_data = cliRead();
          if (rx_data == 'q')
          {
            break;
          }
          else
          {
            uartWrite(uart_ch, &rx_data, 1);
            cliPrintf("-> _DEF_UART%d TX : 0x%X\n", uart_ch + 1, rx_data);            
          }
        }
      }
    }
    else
    {
      cliPrintf("This is cliPort\n");
    }
    ret = true;
  }

  if (ret == false)
  {
    cliPrintf("uart info\n");
    cliPrintf("uart test ch[1~%d]\n", HW_UART_MAX_CH);
  }
}
#endif


#endif

