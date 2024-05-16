#include "led.h"


#ifdef _USE_HW_LED



const typedef struct 
{
  GPIO_T  *port;
  uint32_t pin_port;
  uint32_t pin;
  uint32_t pin_mask;
  uint8_t  on_state;
  uint8_t  off_state;
} led_tbl_t;


static led_tbl_t led_tbl[LED_MAX_CH] = 
{
  {PF, 5, 4, BIT4, _DEF_LOW, _DEF_HIGH},
};



bool ledInit(void)
{

  for (int i=0; i<LED_MAX_CH; i++)
  {
    GPIO_SetSlewCtl(led_tbl[i].port, led_tbl[i].pin_mask, GPIO_SLEWCTL_NORMAL);    
    GPIO_SetPullCtl(led_tbl[i].port, led_tbl[i].pin_mask, GPIO_PUSEL_DISABLE);    
    GPIO_SetMode(led_tbl[i].port, led_tbl[i].pin_mask, GPIO_MODE_OUTPUT);
    ledOff(i);
  }

  return true;
}

void ledOn(uint8_t ch)
{
  if (ch >= LED_MAX_CH) return;

  GPIO_PIN_DATA(led_tbl[ch].pin_port, led_tbl[ch].pin) = led_tbl[ch].on_state;
}

void ledOff(uint8_t ch)
{
  if (ch >= LED_MAX_CH) return;

  GPIO_PIN_DATA(led_tbl[ch].pin_port, led_tbl[ch].pin) = led_tbl[ch].off_state;
}

void ledToggle(uint8_t ch)
{
  if (ch >= LED_MAX_CH) return;

  GPIO_PIN_DATA(led_tbl[ch].pin_port, led_tbl[ch].pin) ^= 1;
}
#endif