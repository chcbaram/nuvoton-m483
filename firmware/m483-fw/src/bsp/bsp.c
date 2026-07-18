#include "bsp.h"
#include "hw_def.h"

volatile static uint32_t systick_ms = 0;

static bool bspInitClock(void);







bool bspInit(void)
{
  bool ret = true;
  uint32_t prioritygroup;

  bspInitClock();

  SysTick_Config(SystemCoreClock / 1000U);

  prioritygroup = NVIC_GetPriorityGrouping();
  NVIC_SetPriority(SysTick_IRQn, NVIC_EncodePriority(prioritygroup, 0, 0));

  return ret;
}

void delay(uint32_t time_ms)
{
#ifdef _USE_HW_RTOS
  if (xTaskGetSchedulerState() != taskSCHEDULER_NOT_STARTED)
  {
    osDelay(ms);
  }
  else
  {
    HAL_Delay(ms);
  }
#else
  uint32_t pre_time = systick_ms;

  while(systick_ms-pre_time < time_ms);
#endif
}

uint32_t millis(void)
{
  return systick_ms;
}

/*
 * 1kHz SysTick 기반 마이크로초 카운터.
 * systick_ms(1ms 단위) + 현재 SysTick 카운터 잔여로 us 를 구성한다.
 * SysTick 언더플로가 아직 서비스되지 않은 경우(PENDSTSET) ms 를 보정해
 * ISR/thread 어디서 호출해도 단조 증가하도록 한다.
 */
uint32_t micros(void)
{
  uint32_t ms;
  uint32_t val;
  uint32_t load = SysTick->LOAD + 1U;
  uint32_t ticks_per_us = SystemCoreClock / 1000000U;
  uint32_t pri = __get_PRIMASK();

  __disable_irq();
  ms  = systick_ms;
  val = SysTick->VAL;
  if (SCB->ICSR & SCB_ICSR_PENDSTSET_Msk)   /* SysTick 언더플로 미처리 -> ms +1 */
  {
    ms++;
    val = SysTick->VAL;
  }
  if (!pri)
    __enable_irq();

  return (ms * 1000U) + ((load - val) / ticks_per_us);
}


bool bspInitClock(void)
{
  /* Unlock protected registers */
  SYS_UnlockReg();

  /* Set XT1_OUT(PF.2) and XT1_IN(PF.3) to input mode */
  PF->MODE &= ~(GPIO_MODE_MODE2_Msk | GPIO_MODE_MODE3_Msk);

  /* Enable HXT clock (external XTAL 12MHz) */
  CLK_EnableXtalRC(CLK_PWRCTL_HXTEN_Msk);

  /* Wait for HXT clock ready */
  CLK_WaitClockReady(CLK_STATUS_HXTSTB_Msk);

  /* Set core clock as PLL_CLOCK from PLL */
  CLK_SetCoreClock(FREQ_192MHZ);

  /* Set PCLK0/PCLK1 to HCLK/2 */
  CLK->PCLKDIV = (CLK_PCLKDIV_APB0DIV_DIV2 | CLK_PCLKDIV_APB1DIV_DIV2);

  return true;
}

void SysTick_Handler(void)
{
  systick_ms++;
}
