#include "i2c.h"

#include <stddef.h>
#include <stdint.h>

#include "mailbox.h"

// Cached after the first successful query — every BSC channel shares the same
// CORE clock, so this only needs to happen once across all of i2c_channel_init().
static uint32_t s_i2c_core_clock_hz = 0;   // 0 = not yet queried

static StatusCode i2c_core_clock_hz(uint32_t *out_hz)
{
  if (s_i2c_core_clock_hz != 0) {
    *out_hz = s_i2c_core_clock_hz;
    return E_OK;
  }

  uint32_t hz;
  StatusCode ret = mbox_get_clock_rate(MBOX_CLOCK_ID_CORE, &hz);
  if (ret != E_OK) {
    return ret;
  }

  s_i2c_core_clock_hz = hz;
  *out_hz = hz;
  return E_OK;
}

// ── Channel table ─────────────────────────────────────────────────────────────
// SoC-fixed facts, never caller-configurable — see docs.md's "Channel table".

typedef struct {
  BSCRegs *regs;
  uint32_t irq_intid;   // 0 = not wired up / unknown — see docs.md
} I2cHwDescriptor;

static const I2cHwDescriptor i2c_hw_table[I2C_NUM_CHANNELS] = {
  [0] = { .regs = (BSCRegs *)I2C0_BASE, .irq_intid = I2C_IRQ_INTID },
  [1] = { .regs = (BSCRegs *)I2C1_BASE, .irq_intid = I2C_IRQ_INTID },
  [2] = { 0 },                                                        // BSC2 — dedicated to HDMI, unsupported gap
  [3] = { .regs = (BSCRegs *)I2C3_BASE, .irq_intid = I2C_IRQ_INTID },
  [4] = { .regs = (BSCRegs *)I2C4_BASE, .irq_intid = I2C_IRQ_INTID }, // placeholder — pins unverified, see docs.md
  [5] = { .regs = (BSCRegs *)I2C5_BASE, .irq_intid = I2C_IRQ_INTID }, // placeholder — pins unverified, see docs.md
  [6] = { .regs = (BSCRegs *)I2C6_BASE, .irq_intid = I2C_IRQ_INTID }, // placeholder — conflicts with I2C0 on GPIO0, see docs.md
};

// Set by i2c_channel_init(); NULL means that channel isn't configured yet.
static I2cConfig *s_i2c_configs[I2C_NUM_CHANNELS] = { NULL };

static inline BSCRegs *i2c_channel_regs(uint8_t channel)
{
  if ((channel >= I2C_NUM_CHANNELS) || (i2c_hw_table[channel].regs == NULL)) {
    return NULL;
  }
  return i2c_hw_table[channel].regs;
}

StatusCode i2c_channel_init(uint8_t channel, I2cConfig *config)
{
  if (config == NULL) {
    return E_INVALID_ARGS;
  }

  BSCRegs *regs = i2c_channel_regs(channel);

  if (regs == NULL) {
    return E_NOTSUPP;
  }

  // Disable before reconfiguring — protects a re-init call from reprogramming
  // DIV/pins out from under a transfer that's still active.
  uint32_t timeout = 10000;
  while ((regs->S & S_TA) && timeout > 0) {
    timeout--;
  }
  if (timeout == 0) {
    return E_TIMED_OUT;
  }
  regs->C = 0;

  gpio_set_function(config->sda_pin, config->alt_func);
  gpio_set_pull(config->sda_pin, GPIO_PULL_UP);
  gpio_set_function(config->scl_pin, config->alt_func);
  gpio_set_pull(config->scl_pin, GPIO_PULL_UP);

  uint32_t core_hz;
  StatusCode ret = i2c_core_clock_hz(&core_hz);
  if (ret != E_OK) {
    return ret;
  }

  uint32_t target_hz = (config->baudrate == I2C_BAUDRATE_FAST_400K)
                       ? I2C_BAUDRATE_FAST_400K_HZ
                       : I2C_BAUDRATE_STANDARD_100K_HZ;

  // Round the divisor up to the nearest even value — CDIV must be even (datasheet).
  uint32_t div = (core_hz + target_hz - 1) / target_hz;
  if (div & 1U) {
    div++;
  }
  if (div < 2U) {
    div = 2U;
  }
  if (div > 0xFFFEU) {
    div = 0xFFFEU;
  }
  regs->DIV = div;

  regs->S = S_CLEAR_ALL;   // clear any stale CLKT/ERR/DONE latches before enabling

  regs->C = C_I2CEN;

  s_i2c_configs[channel] = config;

  return E_OK;
}

void i2c_channel_deinit(uint8_t channel)
{
  BSCRegs *regs = i2c_channel_regs(channel);
  if (regs == NULL) {
    return;
  }

  // Wait for any in-flight transfer to finish before deinit
  uint32_t timeout = 10000;
  while ((regs->S & S_TA) && timeout > 0) {
    timeout--;
  }

  regs->C = 0;              // disable the controller
  regs->S = S_CLEAR_ALL;    // clear any latched status

  I2cConfig *config = s_i2c_configs[channel];
  if (config != NULL) {
    gpio_set_function(config->sda_pin, GPIO_FUNC_INPUT);
    gpio_set_function(config->scl_pin, GPIO_FUNC_INPUT);
  }
}

StatusCode i2c_channel_write(uint8_t channel, uint8_t addr, const uint8_t *buf, uint16_t len)
{
  if (s_i2c_configs[channel] == NULL) {
    return E_NOT_INITIALIZED;
  }

  BSCRegs *regs = i2c_channel_regs(channel);
  if (regs == NULL) {
    return E_NOTSUPP;
  }

  // Clear state
  regs->S = S_CLEAR_ALL;

  regs->A = addr;
  regs->DLEN = len;

  regs->C = C_I2CEN | C_ST | C_WRITE;

  uint32_t index = 0;
  uint32_t timeout = 1000000;   // arbitrary — not calibrated against real transfer timing
  while ((index < len) && timeout > 0) {
    uint32_t status = regs->S;
    if (status & (S_ERR | S_CLKT)) {
      break;   // slave NACK'd or the bus stretched too long — stop feeding the FIFO
    }
    if (status & S_TXD) {
      regs->FIFO = buf[index];
      index++;
    }
    timeout--;
  }

  timeout = 10000;
  while (!(regs->S & (S_DONE | S_ERR | S_CLKT)) && timeout > 0) {
    timeout--;
  }

  uint32_t status = regs->S;
  if (status & S_CLKT) {
    return E_TIMED_OUT;
  }
  if (status & S_ERR) {
    return E_DATA;
  }
  if (!(status & S_DONE)) {
    return E_TIMED_OUT;   // neither loop above ever saw DONE
  }

  return E_OK;
}

StatusCode i2c_channel_read(uint8_t channel, uint8_t addr, uint8_t *buf, uint16_t len)
{
  if (s_i2c_configs[channel] == NULL) {
    return E_NOT_INITIALIZED;
  }

  BSCRegs *regs = i2c_channel_regs(channel);
  if (regs == NULL) {
    return E_NOTSUPP;
  }

  // Clear state
  regs->S = S_CLEAR_ALL;

  regs->A = addr;
  regs->DLEN = len;

  regs->C = C_I2CEN | C_ST | C_READ;

  uint32_t index = 0;
  uint32_t timeout = 1000000;   // arbitrary — not calibrated against real transfer timing
  while ((index < len) && timeout > 0) {
    uint32_t status = regs->S;
    if (status & (S_ERR | S_CLKT)) {
      break;   // slave NACK'd or the bus stretched too long — stop draining the FIFO
    }
    if (status & S_RXD) {
      buf[index] = (uint8_t)regs->FIFO;
      index++;
    }
    timeout--;
  }

  timeout = 10000;
  while (!(regs->S & (S_DONE | S_ERR | S_CLKT)) && timeout > 0) {
    timeout--;
  }

  uint32_t status = regs->S;
  if (status & S_CLKT) {
    return E_TIMED_OUT;
  }
  if (status & S_ERR) {
    return E_DATA;
  }
  if (!(status & S_DONE)) {
    return E_TIMED_OUT;   // neither loop above ever saw DONE
  }

  return E_OK;
}

StatusCode i2c_channel_write_read(uint8_t channel, uint8_t addr,
                                  const uint8_t *tx_buf, uint16_t tx_len,
                                  uint8_t *rx_buf, uint16_t rx_len)
{
  if (s_i2c_configs[channel] == NULL) {
    return E_NOT_INITIALIZED;
  }

  BSCRegs *regs = i2c_channel_regs(channel);
  if (regs == NULL) {
    return E_NOTSUPP;
  }

  if (tx_len > I2C_FIFO_DEPTH) {
    return E_INVALID_ARGS;
  }

  regs->C = C_CLEAR;        // clear the FIFO before loading it
  regs->S = S_CLEAR_ALL;    // clear stale CLKT/ERR/DONE

  regs->A = addr;
  regs->DLEN = tx_len;
  regs->C = C_I2CEN;        // enabled, no ST yet — FIFO can be pre-loaded now

  for (uint16_t i = 0; i < tx_len; i++) {
    regs->FIFO = tx_buf[i];
  }

  regs->C = C_I2CEN | C_ST;   // kick off the write phase

  // Wait for the write phase to be committed to hardware (TA asserted)
  // before reprogramming for the read phase. DONE is accepted too — for a
  // very short write the whole phase can finish before this poll ever
  // observes TA.
  uint32_t timeout = 10000;
  while (!(regs->S & (S_TA | S_DONE | S_ERR | S_CLKT)) && timeout > 0) {
    timeout--;
  }
  uint32_t status = regs->S;
  if (timeout == 0) {
    return E_TIMED_OUT;
  }
  if (status & S_CLKT) {
    return E_TIMED_OUT;
  }
  if (status & S_ERR) {
    return E_DATA;
  }

  // Chain the read: DLEN/C written again while TA is still asserted makes
  // the engine issue a repeated START into read mode instead of a STOP.
  regs->DLEN = rx_len;
  regs->C = C_I2CEN | C_ST | C_READ;

  uint32_t index = 0;
  timeout = 1000000;   // arbitrary — not calibrated against real transfer timing
  while ((index < rx_len) && timeout > 0) {
    status = regs->S;
    if (status & (S_ERR | S_CLKT)) {
      break;   // slave NACK'd or the bus stretched too long — stop draining the FIFO
    }
    if (status & S_RXD) {
      rx_buf[index] = (uint8_t)regs->FIFO;
      index++;
    }
    timeout--;
  }

  timeout = 10000;
  while (!(regs->S & (S_DONE | S_ERR | S_CLKT)) && timeout > 0) {
    timeout--;
  }

  status = regs->S;
  if (status & S_CLKT) {
    return E_TIMED_OUT;
  }
  if (status & S_ERR) {
    return E_DATA;
  }
  if (!(status & S_DONE)) {
    return E_TIMED_OUT;   // neither loop above ever saw DONE
  }

  return E_OK;
}