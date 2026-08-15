#ifndef I2S_H
#define I2S_H

#include <stdbool.h>
#include <stdint.h>

#include "gpio.h"
#include "status.h"

// BCM2711 PCM/I2S peripheral. The CM4 has exactly one instance (unlike
// UART/I2C's multiple BSC/PL011 channels) — no channel table, one static
// config, same single-instance shape as pwm_pca9685's s_config. See docs.md.

#define PCM_BASE 0xFE203000UL

typedef struct {
  volatile uint32_t CS_A;       // 0x00 — control/status (EN, RXON, TXON, FIFO flags)
  volatile uint32_t FIFO_A;     // 0x04 — TX/RX FIFO data
  volatile uint32_t MODE_A;     // 0x08 — frame format (FLEN, FSLEN, CLKM, FSM, CLKI, FSI)
  volatile uint32_t RXC_A;      // 0x0C — RX channel 1/2 width + position
  volatile uint32_t TXC_A;      // 0x10 — TX channel 1/2 width + position
  volatile uint32_t DREQ_A;     // 0x14 — DMA request thresholds (unused — phase 1 is polling-only)
  volatile uint32_t INTEN_A;    // 0x18 — interrupt enables (unused — phase 1 is polling-only)
  volatile uint32_t INTSTC_A;   // 0x1C — interrupt status/clear (unused — phase 1 is polling-only)
  volatile uint32_t GRAY;       // 0x20 — Gray-code mode (unused)
} PCMRegs;

#define PCM ((PCMRegs *)PCM_BASE)

// CS_A — control/status
#define CS_A_EN            (1U << 0)
#define CS_A_RXON          (1U << 1)
#define CS_A_TXON          (1U << 2)
#define CS_A_TXCLR         (1U << 3)
#define CS_A_RXCLR         (1U << 4)
#define CS_A_TXTHR(v)      ((uint32_t)(v) << 5)   // bits 6:5 — TX FIFO threshold, 0-3
#define CS_A_RXTHR(v)      ((uint32_t)(v) << 7)   // bits 8:7 — RX FIFO threshold, 0-3
#define CS_A_DMAEN         (1U << 9)
#define CS_A_TXSYNC        (1U << 13)
#define CS_A_RXSYNC        (1U << 14)
#define CS_A_TXERR         (1U << 15)  // write to TX FIFO while full — write 1 to clear
#define CS_A_RXERR         (1U << 16)  // read from RX FIFO while empty — write 1 to clear
#define CS_A_TXW           (1U << 17)  // TX FIFO at/below threshold — needs writing
#define CS_A_RXR           (1U << 18)  // RX FIFO at/above threshold — needs reading
#define CS_A_TXD           (1U << 19)  // TX FIFO has room for at least one sample
#define CS_A_RXD           (1U << 20)  // RX FIFO has at least one sample
#define CS_A_TXE           (1U << 21)  // TX FIFO empty
#define CS_A_RXF           (1U << 22)  // RX FIFO full
#define CS_A_RXSEX         (1U << 23)  // RX sign extension for widths < 24 bits
#define CS_A_SYNC          (1U << 24)  // read-only — used with STBY for standby resync
#define CS_A_STBY          (1U << 25)

// MODE_A — frame format. FLEN/FSLEN are in PCM_CLK cycles.
#define MODE_A_FSLEN(v)    ((uint32_t)(v) & 0x3FFU)          // bits 9:0
#define MODE_A_FLEN(v)     (((uint32_t)(v) & 0x3FFU) << 10)  // bits 19:10
#define MODE_A_FSI         (1U << 20)  // frame sync invert — 1 = falling edge starts a frame (I2S)
#define MODE_A_FSM         (1U << 21)  // 0 = PCM generates FS (master), 1 = FS is an input (slave)
#define MODE_A_CLKI        (1U << 22)  // clock invert — 1 for standard I2S (NB_NF)
#define MODE_A_CLKM        (1U << 23)  // 0 = PCM generates PCM_CLK (master), 1 = PCM_CLK is an input (slave)
#define MODE_A_FTXP        (1U << 24)  // TX FIFO packed mode (unused — 16-bit samples, not enabled)
#define MODE_A_FRXP         (1U << 25) // RX FIFO packed mode (unused — see above)
#define MODE_A_PDME        (1U << 26)  // PDM input enable (unused)
#define MODE_A_PDMN        (1U << 27)  // PDM decimation factor select (unused)
#define MODE_A_CLKDIS       (1U << 28) // clock disable (debug only)

// TXC_A/RXC_A — two 16-bit channel slots packed into one register.
// Each slot: WID[3:0] (sample width - 8), POS[9:4] (bit offset into the
// frame), EN (bit 14), WEX (bit 15, extends width for >24-bit samples).
#define CHAN_WID(v)        ((uint32_t)(v) & 0xFU)
#define CHAN_POS(v)        (((uint32_t)(v) & 0x3FU) << 4)
#define CHAN_EN            (1U << 14)
#define CHAN_WEX           (1U << 15)
#define CH1(slot)          (((uint32_t)(slot) & 0xFFFFU) << 16)
#define CH2(slot)          ((uint32_t)(slot) & 0xFFFFU)

// DREQ_A — DMA pacing thresholds (unused — phase 1 is polling-only, see docs.md's Phasing)
#define DREQ_A_RX(v)          ((uint32_t)(v) & 0xFFU)
#define DREQ_A_TX(v)          (((uint32_t)(v) & 0xFFU) << 8)
#define DREQ_A_RX_PANIC(v)    (((uint32_t)(v) & 0xFFU) << 16)
#define DREQ_A_TX_PANIC(v)    (((uint32_t)(v) & 0xFFU) << 24)

// INTEN_A/INTSTC_A (unused — phase 1 is polling-only)
#define INT_TXW            (1U << 0)
#define INT_RXR            (1U << 1)
#define INT_TXERR          (1U << 2)
#define INT_RXERR          (1U << 3)

// Standard I2S pins on the CM4 40-pin header, ALT0 — see docs.md.
#define I2S_PIN_CLK         18U   // PCM_CLK
#define I2S_PIN_FS           19U  // PCM_FS
#define I2S_PIN_DIN          20U  // PCM_DIN
#define I2S_PIN_DOUT         21U  // PCM_DOUT

// Phase 1: fixed at 16-bit samples — see docs.md's Phasing before widening this.
#define I2S_BIT_DEPTH        16U
#define I2S_FRAME_LEN_CLKS   (2U * I2S_BIT_DEPTH)   // stereo, one bit-clock frame

typedef enum {
  I2S_SAMPLE_RATE_44100 = 44100,
  I2S_SAMPLE_RATE_48000 = 48000,
} I2sSampleRate;

/**
 * @brief I2S configuration — same shape/ownership rule as UartConfig/I2cConfig.
 * @note Only one PCM peripheral exists on the CM4 — no channel argument, unlike uart_channel_*()/i2c_channel_*().
 */
typedef struct {
  uint8_t clk_pin;
  uint8_t fs_pin;
  uint8_t din_pin;
  uint8_t dout_pin;
  GPIOFunc alt_func;
  I2sSampleRate sample_rate;
} I2sConfig;

/**
 * @brief Configure GPIO pins (ALT0), the PCM clock (via cprman), and the PCM peripheral for full-duplex 16-bit stereo.
 * @note Enables both TXON and RXON — see docs.md if a TX-only or RX-only use case ever needs to skip the other side.
 */
StatusCode i2s_init(I2sConfig *config);

/**
 * @brief Disable the PCM peripheral, stop the PCM clock generator, and release its GPIO pins.
 */
void i2s_deinit(void);

/**
 * @brief Query init state.
 * @return E_OK if initialized, E_NOT_INITIALIZED otherwise.
 */
StatusCode i2s_is_initialized(void);

/**
 * @brief Blocking full-duplex transfer of count interleaved stereo samples (L, R, L, R, ...).
 * @note Polls CS_A.TXD/RXD directly, servicing whichever FIFO is ready each
 * iteration — see docs.md for why this can't be split into independent
 * blocking write()/read() calls without risking an RX overrun.
 * @return E_INVALID_ARGS for a NULL buffer or count == 0; E_TIMED_OUT if
 * neither FIFO makes progress for the spin budget (see docs.md).
 */
StatusCode i2s_transfer(const int16_t *tx, int16_t *rx, uint32_t count);

/**
 * @brief Debug aid: raw CS_A value as of i2s_transfer()'s last poll iteration (its own or the most recent call's).
 * @note Decode against the CS_A_* bitmasks in this header (CS_A_TXD/RXD/TXE/RXF/TXERR/RXERR/...).
 * Returns 0 before the first i2s_transfer() call.
 */
uint32_t i2s_last_status(void);

/**
 * @brief Debug aid: cumulative sample counts moved across every i2s_transfer() call since i2s_init() (or the last i2s_deinit()).
 * @note A monotonically increasing counter here is the simplest liveness
 * signal that FIFO_A accesses are actually happening — see docs.md.
 */
void i2s_transfer_totals(uint32_t *out_tx_total, uint32_t *out_rx_total);

/**
 * @brief Debug aid: how many i2s_transfer() calls have observed CS_A.TXERR/RXERR (write-while-full / read-while-empty) since i2s_init().
 * @note i2s_transfer() clears the latch (write-1-to-clear) once per call
 * after observing it, so this counts distinct occurrences rather than
 * staying stuck at 1 forever after the first one — see docs.md.
 */
uint32_t i2s_error_total(void);

#endif
