#include "i2c.h"


#ifdef _USE_HW_I2C
#include "cli.h"


#if CLI_USE(HW_I2C)
static void cliI2C(cli_args_t *args);
#endif


typedef struct
{
  I2C_T   *p_i2c;
  uint32_t clk_module;
  GPIO_T  *gpio;         /* SCL/SDA 가 속한 GPIO 포트 (버스 복구용) */
  uint32_t port_index;   /* GPIO_PIN_DATA 용 포트 인덱스 (PA=0 ..) */
  uint32_t scl_pin;      /* SCL 핀 번호 */
  uint32_t sda_pin;      /* SDA 핀 번호 */
} i2c_tbl_t;


/* CH0 = I2C1 : SCL=PA.7, SDA=PA.6 (회로도 U18 ZD24C128 EEPROM, R28/R29 5.1k 풀업) */
static const i2c_tbl_t i2c_tbl[I2C_MAX_CH] =
{
  {I2C1, I2C1_MODULE, PA, 0, 7, 6},
};

static bool     is_init  = false;
static bool     is_begin[I2C_MAX_CH];
static uint32_t i2c_freq[I2C_MAX_CH];
static uint32_t i2c_timeout[I2C_MAX_CH];
static uint32_t i2c_errcount[I2C_MAX_CH];


static uint32_t i2cWaitReady(I2C_T *p_i2c, uint32_t timeout);
static void     i2cPortInit(uint8_t ch);

static inline void i2cDelayUs(uint32_t us)
{
  uint32_t t = micros();
  while ((micros() - t) < us);
}


bool i2cInit(void)
{
  for (int i = 0; i < I2C_MAX_CH; i++)
  {
    is_begin[i]     = false;
    i2c_freq[i]     = 400;
    i2c_timeout[i]  = 10;
    i2c_errcount[i] = 0;
  }

  is_init = true;

#if CLI_USE(HW_I2C)
  cliAdd("i2c", cliI2C);
#endif

  return true;
}

bool i2cIsInit(void)
{
  return is_init;
}

static void i2cPortInit(uint8_t ch)
{
  SYS_UnlockReg();

  CLK_EnableModuleClock(i2c_tbl[ch].clk_module);

  /* PA.6 = I2C1_SDA, PA.7 = I2C1_SCL */
  SYS->GPA_MFPL = (SYS->GPA_MFPL & ~(SYS_GPA_MFPL_PA6MFP_Msk | SYS_GPA_MFPL_PA7MFP_Msk)) |
                  (SYS_GPA_MFPL_PA6MFP_I2C1_SDA | SYS_GPA_MFPL_PA7MFP_I2C1_SCL);

  SYS_LockReg();
}

bool i2cBegin(uint8_t ch, uint32_t freq_khz)
{
  if (ch >= I2C_MAX_CH)
    return false;

  i2cPortInit(ch);

  I2C_Open(i2c_tbl[ch].p_i2c, freq_khz * 1000);

  i2c_freq[ch] = freq_khz;
  is_begin[ch] = true;

  return true;
}

bool i2cIsBegin(uint8_t ch)
{
  if (ch >= I2C_MAX_CH)
    return false;

  return is_begin[ch];
}

void i2cReset(uint8_t ch)
{
  if (ch >= I2C_MAX_CH)
    return;

  I2C_Close(i2c_tbl[ch].p_i2c);
  i2cBegin(ch, i2c_freq[ch]);
}

static uint32_t i2cWaitReady(I2C_T *p_i2c, uint32_t timeout)
{
  uint32_t pre_time = millis();

  while (!(p_i2c->CTL0 & I2C_CTL0_SI_Msk))
  {
    if ((millis() - pre_time) >= timeout)
      return 0xFF;
  }
  return I2C_GET_STATUS(p_i2c);
}

/* 전송 실패 처리 : STOP 발행 + 에러 카운트 후 false 반환 */
static bool i2cFail(uint8_t ch)
{
  I2C_SET_CONTROL_REG(i2c_tbl[ch].p_i2c, I2C_CTL_STO | I2C_CTL_SI);
  i2c_errcount[ch]++;
  return false;
}

bool i2cIsDeviceReady(uint8_t ch, uint8_t dev_addr)
{
  I2C_T   *p_i2c;
  uint32_t status;
  uint32_t timeout;

  if (ch >= I2C_MAX_CH)
    return false;

  p_i2c   = i2c_tbl[ch].p_i2c;
  timeout = i2c_timeout[ch];

  I2C_START(p_i2c);
  if (i2cWaitReady(p_i2c, timeout) == 0xFF)
  {
    I2C_SET_CONTROL_REG(p_i2c, I2C_CTL_STO | I2C_CTL_SI);
    return false;
  }

  I2C_SET_DATA(p_i2c, (uint8_t)(dev_addr << 1));   /* SLA+W */
  I2C_SET_CONTROL_REG(p_i2c, I2C_CTL_SI);
  status = i2cWaitReady(p_i2c, timeout);

  I2C_SET_CONTROL_REG(p_i2c, I2C_CTL_STO | I2C_CTL_SI);   /* STOP */

  return (status == 0x18);   /* SLA+W 에 ACK */
}

/*
 * I2C 버스 복구.
 * 슬레이브가 트랜잭션 도중(예: MCU 리셋)에 SDA 를 LOW 로 붙잡고 있으면
 * 페리페럴 리셋만으로는 풀리지 않는다. SCL/SDA 를 GPIO(open-drain)로 전환해
 * SCL 을 최대 9클럭 토글하여 슬레이브가 남은 비트를 내보내고 SDA 를 놓게 한 뒤
 * STOP 조건을 생성한다. 마지막에 핀을 I2C 로 되돌리고 페리페럴을 재초기화한다.
 */
bool i2cRecovery(uint8_t ch)
{
  GPIO_T  *gpio;
  uint32_t pidx;
  uint32_t scl;
  uint32_t sda;
  bool     released;

  if (ch >= I2C_MAX_CH)
    return false;

  gpio = i2c_tbl[ch].gpio;
  pidx = i2c_tbl[ch].port_index;
  scl  = i2c_tbl[ch].scl_pin;
  sda  = i2c_tbl[ch].sda_pin;

  /* 핀을 GPIO 로 전환 (MFP = 0) */
  SYS_UnlockReg();
  SYS->GPA_MFPL &= ~(SYS_GPA_MFPL_PA6MFP_Msk | SYS_GPA_MFPL_PA7MFP_Msk);
  SYS_LockReg();

  /* open-drain 으로 두고 둘 다 해제(HIGH). 외부 5.1k 풀업이 HIGH 로 끌어올림 */
  GPIO_SetMode(gpio, (1UL << scl), GPIO_MODE_OPEN_DRAIN);
  GPIO_SetMode(gpio, (1UL << sda), GPIO_MODE_OPEN_DRAIN);
  GPIO_PIN_DATA(pidx, scl) = 1;
  GPIO_PIN_DATA(pidx, sda) = 1;
  i2cDelayUs(5);

  /* SDA 가 풀릴 때까지 SCL 최대 9클럭 (약 100kHz) */
  for (int i = 0; i < 9; i++)
  {
    if (GPIO_PIN_DATA(pidx, sda) != 0)
      break;

    GPIO_PIN_DATA(pidx, scl) = 0;
    i2cDelayUs(5);
    GPIO_PIN_DATA(pidx, scl) = 1;
    i2cDelayUs(5);
  }

  /* STOP 조건 : SCL HIGH 인 상태에서 SDA LOW -> HIGH */
  GPIO_PIN_DATA(pidx, sda) = 0;
  i2cDelayUs(5);
  GPIO_PIN_DATA(pidx, scl) = 1;
  i2cDelayUs(5);
  GPIO_PIN_DATA(pidx, sda) = 1;
  i2cDelayUs(5);

  released = (GPIO_PIN_DATA(pidx, sda) != 0);

  /* 핀을 I2C 로 되돌리고 페리페럴 재초기화 */
  i2cReset(ch);

  return released;
}

/*--------------------------------------------------------------------------*/
/* Register-addressed read/write (8-bit / 16-bit reg addr)                  */
/*--------------------------------------------------------------------------*/
/*
 * 저수준 레지스터 읽기 (reg_len = 1 또는 2 바이트 주소).
 * Nuvoton StdDriver 의 I2C_ReadMultiBytes*Reg 는 case 0x40 에서 무조건 ACK 를
 * 설정해 1바이트 읽기 시 항상 2바이트를 읽고 버퍼를 넘겨 쓰는 버그가 있어,
 * 마지막 바이트에서 NACK 를 내도록 직접 구현한다.
 */
static bool i2cReadReg(uint8_t ch, uint8_t dev_addr, uint16_t reg_addr, uint8_t reg_len,
                       uint8_t *p_data, uint32_t length, uint32_t timeout)
{
  I2C_T   *p_i2c;
  uint8_t  addr_buf[2];
  uint8_t  addr_n;
  uint32_t i;

  if (ch >= I2C_MAX_CH || length == 0)
    return false;

  p_i2c = i2c_tbl[ch].p_i2c;

  if (reg_len == 2)
  {
    addr_buf[0] = (uint8_t)((reg_addr >> 8) & 0xFF);
    addr_buf[1] = (uint8_t)(reg_addr & 0xFF);
    addr_n = 2;
  }
  else
  {
    addr_buf[0] = (uint8_t)(reg_addr & 0xFF);
    addr_n = 1;
  }

  /* START -> SLA+W */
  I2C_START(p_i2c);
  if (i2cWaitReady(p_i2c, timeout) != 0x08) return i2cFail(ch);

  I2C_SET_DATA(p_i2c, (uint8_t)(dev_addr << 1));
  I2C_SET_CONTROL_REG(p_i2c, I2C_CTL_SI);
  if (i2cWaitReady(p_i2c, timeout) != 0x18) return i2cFail(ch);

  /* register address (MSB first) */
  for (i = 0; i < addr_n; i++)
  {
    I2C_SET_DATA(p_i2c, addr_buf[i]);
    I2C_SET_CONTROL_REG(p_i2c, I2C_CTL_SI);
    if (i2cWaitReady(p_i2c, timeout) != 0x28) return i2cFail(ch);
  }

  /* repeated START -> SLA+R */
  I2C_SET_CONTROL_REG(p_i2c, I2C_CTL_STA | I2C_CTL_SI);
  if (i2cWaitReady(p_i2c, timeout) != 0x10) return i2cFail(ch);

  I2C_SET_DATA(p_i2c, (uint8_t)((dev_addr << 1) | 0x01));
  I2C_SET_CONTROL_REG(p_i2c, I2C_CTL_SI);
  if (i2cWaitReady(p_i2c, timeout) != 0x40) return i2cFail(ch);

  /* read data : ACK 이후 바이트들, 마지막 바이트는 NACK */
  for (i = 0; i < length; i++)
  {
    if (i == (length - 1))
      I2C_SET_CONTROL_REG(p_i2c, I2C_CTL_SI);                  /* NACK last */
    else
      I2C_SET_CONTROL_REG(p_i2c, I2C_CTL_SI | I2C_CTL_AA);     /* ACK */

    if (i2cWaitReady(p_i2c, timeout) != ((i == (length - 1)) ? 0x58u : 0x50u)) return i2cFail(ch);

    p_data[i] = I2C_GET_DATA(p_i2c);
  }

  I2C_SET_CONTROL_REG(p_i2c, I2C_CTL_STO | I2C_CTL_SI);
  return true;
}

bool i2cReadByte(uint8_t ch, uint16_t dev_addr, uint16_t reg_addr, uint8_t *p_data, uint32_t timeout)
{
  return i2cReadReg(ch, (uint8_t)dev_addr, reg_addr, 1, p_data, 1, timeout);
}

bool i2cReadBytes(uint8_t ch, uint16_t dev_addr, uint16_t reg_addr, uint8_t *p_data, uint32_t length, uint32_t timeout)
{
  return i2cReadReg(ch, (uint8_t)dev_addr, reg_addr, 1, p_data, length, timeout);
}

bool i2cReadA16Bytes(uint8_t ch, uint16_t dev_addr, uint16_t reg_addr, uint8_t *p_data, uint32_t length, uint32_t timeout)
{
  return i2cReadReg(ch, (uint8_t)dev_addr, reg_addr, 2, p_data, length, timeout);
}

bool i2cWriteByte(uint8_t ch, uint16_t dev_addr, uint16_t reg_addr, uint8_t data, uint32_t timeout)
{
  return i2cWriteBytes(ch, dev_addr, reg_addr, &data, 1, timeout);
}

bool i2cWriteBytes(uint8_t ch, uint16_t dev_addr, uint16_t reg_addr, uint8_t *p_data, uint32_t length, uint32_t timeout)
{
  uint32_t ret_len;
  (void)timeout;

  if (ch >= I2C_MAX_CH)
    return false;

  ret_len = I2C_WriteMultiBytesOneReg(i2c_tbl[ch].p_i2c, (uint8_t)dev_addr, (uint8_t)reg_addr, p_data, length);
  if (ret_len != length)
  {
    i2c_errcount[ch]++;
    return false;
  }
  return true;
}

bool i2cWriteA16Bytes(uint8_t ch, uint16_t dev_addr, uint16_t reg_addr, uint8_t *p_data, uint32_t length, uint32_t timeout)
{
  uint32_t ret_len;
  (void)timeout;

  if (ch >= I2C_MAX_CH)
    return false;

  ret_len = I2C_WriteMultiBytesTwoRegs(i2c_tbl[ch].p_i2c, (uint8_t)dev_addr, reg_addr, p_data, length);
  if (ret_len != length)
  {
    i2c_errcount[ch]++;
    return false;
  }
  return true;
}

/*--------------------------------------------------------------------------*/
/* Raw read/write (no register address)                                     */
/*--------------------------------------------------------------------------*/
bool i2cReadData(uint8_t ch, uint16_t dev_addr, uint8_t *p_data, uint32_t length, uint32_t timeout)
{
  I2C_T   *p_i2c;
  uint32_t status;
  uint32_t i;

  if (ch >= I2C_MAX_CH || length == 0)
    return false;

  p_i2c = i2c_tbl[ch].p_i2c;

  I2C_START(p_i2c);
  if (i2cWaitReady(p_i2c, timeout) == 0xFF) return i2cFail(ch);

  I2C_SET_DATA(p_i2c, (uint8_t)((dev_addr << 1) | 0x01));   /* SLA+R */
  I2C_SET_CONTROL_REG(p_i2c, I2C_CTL_SI);
  if (i2cWaitReady(p_i2c, timeout) != 0x40) return i2cFail(ch);   /* SLA+R ACK */

  for (i = 0; i < length; i++)
  {
    if (i == (length - 1))
      I2C_SET_CONTROL_REG(p_i2c, I2C_CTL_SI);               /* 마지막: NACK */
    else
      I2C_SET_CONTROL_REG(p_i2c, I2C_CTL_SI | I2C_CTL_AA);  /* ACK */

    status = i2cWaitReady(p_i2c, timeout);
    if (status == 0xFF) return i2cFail(ch);

    p_data[i] = I2C_GET_DATA(p_i2c);
  }

  I2C_SET_CONTROL_REG(p_i2c, I2C_CTL_STO | I2C_CTL_SI);
  return true;
}

bool i2cWriteData(uint8_t ch, uint16_t dev_addr, uint8_t *p_data, uint32_t length, uint32_t timeout)
{
  I2C_T   *p_i2c;
  uint32_t i;

  if (ch >= I2C_MAX_CH)
    return false;

  p_i2c = i2c_tbl[ch].p_i2c;

  I2C_START(p_i2c);
  if (i2cWaitReady(p_i2c, timeout) == 0xFF) return i2cFail(ch);

  I2C_SET_DATA(p_i2c, (uint8_t)(dev_addr << 1));           /* SLA+W */
  I2C_SET_CONTROL_REG(p_i2c, I2C_CTL_SI);
  if (i2cWaitReady(p_i2c, timeout) != 0x18) return i2cFail(ch);   /* SLA+W ACK */

  for (i = 0; i < length; i++)
  {
    I2C_SET_DATA(p_i2c, p_data[i]);
    I2C_SET_CONTROL_REG(p_i2c, I2C_CTL_SI);
    if (i2cWaitReady(p_i2c, timeout) != 0x28) return i2cFail(ch);   /* data ACK */
  }

  I2C_SET_CONTROL_REG(p_i2c, I2C_CTL_STO | I2C_CTL_SI);
  return true;
}

/*--------------------------------------------------------------------------*/
void i2cSetTimeout(uint8_t ch, uint32_t timeout)
{
  if (ch >= I2C_MAX_CH) return;
  i2c_timeout[ch] = timeout;
}

uint32_t i2cGetTimeout(uint8_t ch)
{
  if (ch >= I2C_MAX_CH) return 0;
  return i2c_timeout[ch];
}

void i2cClearErrCount(uint8_t ch)
{
  if (ch >= I2C_MAX_CH) return;
  i2c_errcount[ch] = 0;
}

uint32_t i2cGetErrCount(uint8_t ch)
{
  if (ch >= I2C_MAX_CH) return 0;
  return i2c_errcount[ch];
}


#if CLI_USE(HW_I2C)
static void cliI2C(cli_args_t *args)
{
  bool ret = false;

  if (args->argc == 2 && args->isStr(0, "scan"))
  {
    uint8_t ch = (uint8_t)args->getData(1);

    if (ch >= I2C_MAX_CH)
    {
      cliPrintf("ch error\n");
      return;
    }

    if (!is_begin[ch])
      i2cBegin(ch, 400);

    cliPrintf("i2c ch%d scan (7-bit addr)\n", ch);
    for (int addr = 0x08; addr < 0x78; addr++)
    {
      if (i2cIsDeviceReady(ch, addr))
        cliPrintf("  found : 0x%02X\n", addr);
    }
    cliPrintf("done\n");
    ret = true;
  }

  if (args->argc == 1 && args->isStr(0, "info"))
  {
    for (int i = 0; i < I2C_MAX_CH; i++)
    {
      cliPrintf("ch%d begin:%s freq:%dkHz err:%d\n",
                i, is_begin[i] ? "yes" : "no", (int)i2c_freq[i], (int)i2c_errcount[i]);
    }
    ret = true;
  }

  if (ret == false)
  {
    cliPrintf("i2c scan [ch]\n");
    cliPrintf("i2c info\n");
  }
}
#endif

#endif /* _USE_HW_I2C */
