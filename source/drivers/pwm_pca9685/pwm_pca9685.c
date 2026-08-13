#include "pwm_pca9685.h"

#include <stddef.h>
#include <stdint.h>

#include "i2c.h"
#include "scheduler.h"   // task_delay_ms()
#include "uart.h"        // debug prints in pwm_pca9685_init()'s failure path

// Set by pwm_pca9685_init(); NULL means not initialized. Caller-owned,
// static/global storage duration — same ownership rule as UartConfig/I2cConfig.
static Pca9685Config *s_config = NULL;

// Both helpers take channel/addr explicitly (not read from s_config) so
// pwm_pca9685_init() can use them before committing s_config — see its
// comment for why.
static StatusCode pca9685_write_reg(uint8_t channel, uint8_t i2c_addr, uint8_t reg, uint8_t value)
{
  uint8_t buf[2] = { reg, value };
  return i2c_channel_write(channel, i2c_addr, buf, 2);
}

// Deliberately two independent transactions (STOP between them), not
// i2c_channel_write_read()'s repeated-start technique — the PCA9685 doesn't
// need a true repeated start for a register read (Adafruit's widely-used
// PCA9685 library does the equivalent of this same STOP-then-START sequence
// via Wire.endTransmission()/requestFrom()), and the repeated-start hand-off
// is real hardware's less-verified path — see i2c/docs.md's "Repeated start"
// section for why that matters here specifically.
static StatusCode pca9685_read_reg(uint8_t channel, uint8_t i2c_addr, uint8_t reg, uint8_t *out_value)
{
  StatusCode ret = i2c_channel_write(channel, i2c_addr, &reg, 1);
  if (ret != E_OK) {
    return ret;
  }
  return i2c_channel_read(channel, i2c_addr, out_value, 1);
}

StatusCode pwm_pca9685_is_initialized(void)
{
  return (s_config != NULL) ? E_OK : E_NOT_INITIALIZED;
}

// Logs which init step failed and the raw I2C status at that point (decode
// against i2c.h's S_* bitmasks — S_ERR is a NACK, S_CLKT a clock-stretch
// timeout, and 0x00 with a timeout return means the transaction never
// registered any status at all). Returns ret unchanged so call sites can
// just `return pca9685_init_step_failed(...)`.
static StatusCode pca9685_init_step_failed(uint8_t channel, const char *step, StatusCode ret)
{
  uart_printf("pwm_pca9685: init step '%s' failed: ret=%d S=0x%02X\r\n",
              step, ret, i2c_channel_last_status(channel));
  return ret;
}

StatusCode pwm_pca9685_init(Pca9685Config *config)
{
  if (config == NULL) {
    return E_INVALID_ARGS;
  }
  if ((config->pwm_freq_hz < PCA9685_FREQ_HZ_MIN) || (config->pwm_freq_hz > PCA9685_FREQ_HZ_MAX)) {
    return E_INVALID_ARGS;
  }

  uint8_t channel = config->channel;
  uint8_t i2c_addr = config->i2c_addr;

  uint8_t mode1;
  StatusCode ret = pca9685_read_reg(channel, i2c_addr, PCA9685_MODE1, &mode1);
  if (ret != E_OK) {
    return pca9685_init_step_failed(channel, "read MODE1", ret);
  }

  uint8_t mode1_sleep = (mode1 & (uint8_t) ~PCA9685_MODE1_RESTART) | PCA9685_MODE1_SLEEP;
  ret = pca9685_write_reg(channel, i2c_addr, PCA9685_MODE1, mode1_sleep);
  if (ret != E_OK) {
    return pca9685_init_step_failed(channel, "write MODE1 (sleep)", ret);
  }

  // PRE_SCALE = round(osc_clock / (4096 * update_rate)) - 1 — datasheet
  // formula. Integer round-to-nearest via +denom/2 before dividing, then
  // subtract 1 (rounding commutes with subtracting a whole number).
  uint32_t denom = PCA9685_TICK_MAX * config->pwm_freq_hz;
  uint32_t prescale = (PCA9685_OSC_CLOCK_HZ + (denom / 2U)) / denom;
  uint8_t prescale_val = (uint8_t)(prescale - 1U);

  ret = pca9685_write_reg(channel, i2c_addr, PCA9685_PRE_SCALE, prescale_val);
  if (ret != E_OK) {
    return pca9685_init_step_failed(channel, "write PRE_SCALE", ret);
  }

  ret = pca9685_write_reg(channel, i2c_addr, PCA9685_MODE1, mode1_sleep & (uint8_t) ~PCA9685_MODE1_SLEEP);
  if (ret != E_OK) {
    return pca9685_init_step_failed(channel, "write MODE1 (clear sleep)", ret);
  }

  // Datasheet: wait >= 500us after clearing SLEEP before setting RESTART.
  // One scheduler tick is >= that on every hz this project's samples use
  // (hz=1000 -> 1ms/tick); revisit if a much higher tick rate is ever configured.
  task_delay_ms(1);

  ret = pca9685_write_reg(channel, i2c_addr, PCA9685_MODE1,
                          (mode1_sleep & (uint8_t) ~PCA9685_MODE1_SLEEP) | PCA9685_MODE1_RESTART | PCA9685_MODE1_AI);
  if (ret != E_OK) {
    return pca9685_init_step_failed(channel, "write MODE1 (restart)", ret);
  }

  ret = pca9685_write_reg(channel, i2c_addr, PCA9685_MODE2, PCA9685_MODE2_OUTDRV);
  if (ret != E_OK) {
    return pca9685_init_step_failed(channel, "write MODE2", ret);
  }

  s_config = config;
  return E_OK;
}

StatusCode pwm_pca9685_deinit(void)
{
  if (s_config == NULL) {
    return E_NOT_INITIALIZED;
  }

  for (uint8_t ch = 0; ch < PCA9685_NUM_CHANNELS; ch++) {
    pwm_pca9685_set_channel_full_off(ch);   // best-effort — see docs.md
  }

  StatusCode ret = pca9685_write_reg(s_config->channel, s_config->i2c_addr, PCA9685_MODE1, PCA9685_MODE1_SLEEP);

  s_config = NULL;
  return ret;
}

StatusCode pwm_pca9685_set_channel(uint8_t pwm_channel, uint32_t delay, uint32_t duty_cycle)
{
  if (s_config == NULL) {
    return E_NOT_INITIALIZED;
  }
  if (pwm_channel >= PCA9685_NUM_CHANNELS) {
    return E_INVALID_ARGS;
  }
  // Mirrors the original's delay_percentage + duty_cycle > 1.0f check, but as
  // an overflow test on the uint32_t scale instead of a float comparison.
  if ((UINT32_MAX - delay) < duty_cycle) {
    return E_INVALID_ARGS;
  }

  // Scale each fraction (0..UINT32_MAX) down to a 12-bit tick position
  // (0..4095). 64-bit intermediate avoids overflow: delay/duty_cycle *
  // (TICK_MAX-1) can exceed 32 bits before the divide.
  uint16_t on_tick = (uint16_t)(((uint64_t)delay * (PCA9685_TICK_MAX - 1U)) / UINT32_MAX);
  uint16_t off_tick = (uint16_t)(((uint64_t)(delay + duty_cycle) * (PCA9685_TICK_MAX - 1U)) / UINT32_MAX);

  uint8_t reg = (uint8_t)(PCA9685_LED0_ON_L + 4U * pwm_channel);
  uint8_t buf[5];
  buf[0] = reg;
  buf[1] = (uint8_t)(on_tick & 0xFFU);
  buf[2] = (uint8_t)((on_tick >> 8) & 0x0FU);
  buf[3] = (uint8_t)(off_tick & 0xFFU);
  buf[4] = (uint8_t)((off_tick >> 8) & 0x0FU);

  return i2c_channel_write(s_config->channel, s_config->i2c_addr, buf, 5);
}

StatusCode pwm_pca9685_set_channel_full_off(uint8_t pwm_channel)
{
  if (s_config == NULL) {
    return E_NOT_INITIALIZED;
  }
  if (pwm_channel >= PCA9685_NUM_CHANNELS) {
    return E_INVALID_ARGS;
  }

  uint8_t base = (uint8_t)(PCA9685_LED0_ON_L + 4U * pwm_channel);

  // Clear ON_H's full-on override, then set OFF_H's full-off override —
  // datasheet's documented full-off sequence.
  StatusCode ret = pca9685_write_reg(s_config->channel, s_config->i2c_addr, (uint8_t)(base + 1U), 0x00U);
  if (ret != E_OK) {
    return ret;
  }
  return pca9685_write_reg(s_config->channel, s_config->i2c_addr, (uint8_t)(base + 3U), PCA9685_LED_FULL_BIT);
}

StatusCode pwm_pca9685_set_channel_full_on(uint8_t pwm_channel)
{
  if (s_config == NULL) {
    return E_NOT_INITIALIZED;
  }
  if (pwm_channel >= PCA9685_NUM_CHANNELS) {
    return E_INVALID_ARGS;
  }

  uint8_t base = (uint8_t)(PCA9685_LED0_ON_L + 4U * pwm_channel);

  // Clear OFF_H's full-off override, then set ON_H's full-on override.
  StatusCode ret = pca9685_write_reg(s_config->channel, s_config->i2c_addr, (uint8_t)(base + 3U), 0x00U);
  if (ret != E_OK) {
    return ret;
  }
  return pca9685_write_reg(s_config->channel, s_config->i2c_addr, (uint8_t)(base + 1U), PCA9685_LED_FULL_BIT);
}