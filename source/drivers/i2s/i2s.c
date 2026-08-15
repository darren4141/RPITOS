#include "i2s.h"

#include <stddef.h>

#include "cprman.h"

// Arbitrary spin budgets — not calibrated against real transfer/settle
// timing, same caveat every other driver's polling timeout in this codebase
// carries (see i2c/docs.md).
#define I2S_CLEAR_SETTLE_SPINS   1000
#define I2S_TRANSFER_SPIN_BUDGET 1000000

// Standard I2S (Philips) framing, per channel slot: MSB lands one PCM_CLK
// cycle after the FS edge ("data_delay" in the Linux driver this was cross-
// checked against — see docs.md). Channel 1 (left) starts at that 1-cycle
// offset; channel 2 (right) starts I2S_BIT_DEPTH cycles later.
#define I2S_DATA_DELAY   1U
#define I2S_CH1_POS      (I2S_DATA_DELAY)
#define I2S_CH2_POS      (I2S_BIT_DEPTH + I2S_DATA_DELAY)
#define I2S_CHAN_WID_VAL (I2S_BIT_DEPTH - 8U)

// The CS_A bits that must stay asserted for the engine to keep running —
// reused so the error-clear write below (and any other partial CS_A write)
// re-asserts these instead of accidentally dropping them. CS_A packs
// level/config bits (EN, TXON, RXON, TXTHR, RXTHR — must be written back
// every time) alongside write-1-to-clear event bits (TXERR, RXERR, ...) in
// the same register; a write that only sets the W1C bits without also
// re-asserting these would silently disable the engine. See docs.md.
#define I2S_CS_A_RUN_BITS (CS_A_EN | CS_A_TXTHR(1) | CS_A_RXTHR(1) | CS_A_TXON | CS_A_RXON)

// Set by i2s_init(); NULL means not configured yet — mirrors pwm_pca9685's
// single static config pointer (this SoC has exactly one PCM peripheral).
static I2sConfig *s_i2s_config = NULL;

// Debug aids only — see i2s_last_status()/i2s_transfer_totals()/
// i2s_error_total() and docs.md. Reset on i2s_init()/i2s_deinit() so totals
// track "since this config was brought up," not a lifetime-of-the-process count.
static uint32_t s_i2s_last_cs_a = 0;
static uint32_t s_i2s_tx_total = 0;
static uint32_t s_i2s_rx_total = 0;
static uint32_t s_i2s_error_total = 0;

StatusCode i2s_is_initialized(void)
{
  return (s_i2s_config != NULL) ? E_OK : E_NOT_INITIALIZED;
}

StatusCode i2s_init(I2sConfig *config)
{
  if (config == NULL) {
    return E_INVALID_ARGS;
  }

  // Disable before reconfiguring — same rationale as i2c_channel_init()'s
  // unconditional regs->C = 0: protects a re-init call from reprogramming
  // MODE_A/TXC_A/RXC_A out from under a still-running engine.
  PCM->CS_A = 0;

  gpio_set_function(config->clk_pin, config->alt_func);
  gpio_set_function(config->fs_pin, config->alt_func);
  gpio_set_function(config->din_pin, config->alt_func);
  gpio_set_function(config->dout_pin, config->alt_func);

  uint32_t bitclock_hz = (uint32_t)config->sample_rate * I2S_FRAME_LEN_CLKS;
  StatusCode ret = cprman_pcm_clock_enable(bitclock_hz);
  if (ret != E_OK) {
    return ret;
  }

  // MODE_A: CLKM=0/FSM=0 (both clear — CM4 is bus master, generates
  // PCM_CLK/PCM_FS), CLKI=1/FSI=1 (standard I2S polarity, NB_NF format).
  PCM->MODE_A = MODE_A_FLEN(I2S_FRAME_LEN_CLKS - 1U) | MODE_A_FSLEN(I2S_FRAME_LEN_CLKS / 2U)
               | MODE_A_FSI | MODE_A_CLKI;

  uint32_t chan_slot = CHAN_EN | CHAN_WID(I2S_CHAN_WID_VAL);
  uint32_t ch1 = CH1(chan_slot | CHAN_POS(I2S_CH1_POS));
  uint32_t ch2 = CH2(chan_slot | CHAN_POS(I2S_CH2_POS));
  PCM->TXC_A = ch1 | ch2;
  PCM->RXC_A = ch1 | ch2;

  // TXCLR/RXCLR need a couple of PCM_CLK cycles to actually flush before
  // they can be cleared again — see docs.md's "FIFO clear timing" open item.
  PCM->CS_A = CS_A_EN | CS_A_TXCLR | CS_A_RXCLR;
  for (volatile uint32_t i = 0; i < I2S_CLEAR_SETTLE_SPINS; i++) {}

  // TXERR|RXERR here clears whatever the TXCLR/RXCLR pulse above may have
  // latched, so a fresh init always starts from a clean error state instead
  // of carrying forward a stale (and now meaningless) flag — see docs.md.
  PCM->CS_A = I2S_CS_A_RUN_BITS | CS_A_TXERR | CS_A_RXERR;

  s_i2s_last_cs_a = 0;
  s_i2s_tx_total = 0;
  s_i2s_rx_total = 0;
  s_i2s_error_total = 0;

  s_i2s_config = config;
  return E_OK;
}

void i2s_deinit(void)
{
  if (s_i2s_config == NULL) {
    return;
  }

  PCM->CS_A = 0;
  cprman_pcm_clock_disable();

  gpio_set_function(s_i2s_config->clk_pin, GPIO_FUNC_INPUT);
  gpio_set_function(s_i2s_config->fs_pin, GPIO_FUNC_INPUT);
  gpio_set_function(s_i2s_config->din_pin, GPIO_FUNC_INPUT);
  gpio_set_function(s_i2s_config->dout_pin, GPIO_FUNC_INPUT);

  s_i2s_config = NULL;
  s_i2s_last_cs_a = 0;
  s_i2s_tx_total = 0;
  s_i2s_rx_total = 0;
  s_i2s_error_total = 0;
}

StatusCode i2s_transfer(const int16_t *tx, int16_t *rx, uint32_t count)
{
  if (s_i2s_config == NULL) {
    return E_NOT_INITIALIZED;
  }
  if ((tx == NULL) || (rx == NULL) || (count == 0)) {
    return E_INVALID_ARGS;
  }

  uint32_t tx_idx = 0;
  uint32_t rx_idx = 0;
  uint32_t timeout = I2S_TRANSFER_SPIN_BUDGET;

  // Both FIFOs are serviced in the same loop, not two independent
  // write()/read() calls — see docs.md for why splitting this would risk an
  // RX overrun (RXON keeps sampling continuously off the shared PCM_CLK/
  // PCM_FS regardless of whether the caller is currently draining it).
  while (((tx_idx < count) || (rx_idx < count)) && (timeout > 0)) {
    uint32_t cs = PCM->CS_A;
    s_i2s_last_cs_a = cs;
    bool progressed = false;

    if ((tx_idx < count) && (cs & CS_A_TXD)) {
      PCM->FIFO_A = (uint32_t)(uint16_t)tx[tx_idx];
      tx_idx++;
      s_i2s_tx_total++;
      progressed = true;
    }
    if ((rx_idx < count) && (cs & CS_A_RXD)) {
      rx[rx_idx] = (int16_t)(PCM->FIFO_A & 0xFFFFU);
      rx_idx++;
      s_i2s_rx_total++;
      progressed = true;
    }

    if (progressed) {
      timeout = I2S_TRANSFER_SPIN_BUDGET;
    }
    else {
      timeout--;
    }
  }

  // TXERR/RXERR are write-1-to-clear — left alone, a single latch from
  // anywhere would read as "still set" on every future status check
  // forever, making i2s_last_status() useless for telling a stale one-time
  // event apart from a live, recurring one. Clearing here (once per call,
  // not per FIFO access — see docs.md on why per-access clearing isn't
  // worth the extra CS_A write in the hot loop) turns s_i2s_error_total
  // into a real "how many transfers have seen this since init" count.
  uint32_t final_cs = PCM->CS_A;
  if (final_cs & (CS_A_TXERR | CS_A_RXERR)) {
    s_i2s_error_total++;
    PCM->CS_A = I2S_CS_A_RUN_BITS | (final_cs & (CS_A_TXERR | CS_A_RXERR));
  }

  return ((tx_idx == count) && (rx_idx == count)) ? E_OK : E_TIMED_OUT;
}

uint32_t i2s_last_status(void)
{
  return s_i2s_last_cs_a;
}

void i2s_transfer_totals(uint32_t *out_tx_total, uint32_t *out_rx_total)
{
  if (out_tx_total != NULL) {
    *out_tx_total = s_i2s_tx_total;
  }
  if (out_rx_total != NULL) {
    *out_rx_total = s_i2s_rx_total;
  }
}

uint32_t i2s_error_total(void)
{
  return s_i2s_error_total;
}
