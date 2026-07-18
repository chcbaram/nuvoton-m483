#include "button.h"


#ifdef _USE_HW_BUTTON
#include "cli.h"


typedef struct
{
  GPIO_T  *port;
  uint32_t pin_port;    /* GPIO_PIN_DATA 용 포트 인덱스 (PA=0, PB=1, PC=2, ...) */
  uint32_t pin;
  uint32_t pin_mask;
  uint8_t  on_state;    /* 눌렸을 때의 핀 레벨 */
  const char *p_name;
} button_tbl_t;


/* BTN1 = PC.14, 외부 10k 풀업, 액티브-로우 (눌림 = LOW) */
static const button_tbl_t button_tbl[BUTTON_MAX_CH] =
{
  {PC, 2, 14, BIT14, _DEF_LOW, "BTN1"},
};

static bool is_init = false;

#ifdef _USE_HW_CLI
static void cliButton(cli_args_t *args);
#endif


bool buttonInit(void)
{
  for (int i = 0; i < BUTTON_MAX_CH; i++)
  {
    GPIO_SetMode(button_tbl[i].port, button_tbl[i].pin_mask, GPIO_MODE_INPUT);
    GPIO_SetPullCtl(button_tbl[i].port, button_tbl[i].pin_mask, GPIO_PUSEL_PULL_UP);
  }

  is_init = true;

#ifdef _USE_HW_CLI
  cliAdd("button", cliButton);
#endif

  return true;
}

bool buttonGetPressed(uint8_t ch)
{
  if (ch >= BUTTON_MAX_CH)
    return false;

  return (GPIO_PIN_DATA(button_tbl[ch].pin_port, button_tbl[ch].pin) == button_tbl[ch].on_state);
}

uint32_t buttonGetData(void)
{
  uint32_t ret = 0;

  for (int i = 0; i < BUTTON_MAX_CH; i++)
  {
    if (buttonGetPressed(i))
      ret |= (1 << i);
  }

  return ret;
}

const char *buttonGetName(uint8_t ch)
{
  if (ch >= BUTTON_MAX_CH)
    return "NONE";

  return button_tbl[ch].p_name;
}


#ifdef _USE_HW_CLI
static void cliButton(cli_args_t *args)
{
  bool ret = false;

  if (args->argc == 1 && args->isStr(0, "info"))
  {
    cliPrintf("button init : %s\n", is_init ? "yes" : "no");
    for (int i = 0; i < BUTTON_MAX_CH; i++)
    {
      cliPrintf("  %-6s : %s\n", buttonGetName(i), buttonGetPressed(i) ? "PRESSED" : "-");
    }
    ret = true;
  }

  if (args->argc == 1 && args->isStr(0, "show"))
  {
    while (cliKeepLoop())
    {
      cliPrintf("button 0x%02X : ", (int)buttonGetData());
      for (int i = 0; i < BUTTON_MAX_CH; i++)
        cliPrintf("%s ", buttonGetPressed(i) ? "O" : ".");
      cliPrintf("\r");
      delay(50);
    }
    cliPrintf("\n");
    ret = true;
  }

  if (ret == false)
  {
    cliPrintf("button info\n");
    cliPrintf("button show\n");
  }
}
#endif

#endif /* _USE_HW_BUTTON */
