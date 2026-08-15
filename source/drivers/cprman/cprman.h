#ifndef CPRMAN_H
#define CPRMAN_H

#include <stdbool.h>
#include <stdint.h>

#include "status.h"

// CPRMAN (Clock/Power/Reset MANager) — the SoC's central clock generator.
// Scope: PCM clock generator only, for i2s/ — see docs.md before extending
// this to another CM_xxx clock (PWM, GPCLK, ...).

#define CPRMAN_BASE      0xFE101000UL

// CM_PCMCTL/CM_PCMDIV are adjacent words — see docs.md's "Register offsets".
typedef struct {
  volatile uint32_t CTL;   // 0x00 (CM_PCMCTL) — password | MASH | FLIP | BUSY | KILL | ENAB | SRC
  volatile uint32_t DIV;   // 0x04 (CM_PCMDIV) — password | DIVI[23:12] | DIVF[11:0]
} CprmanClockRegs;

#define CPRMAN_PCM ((CprmanClockRegs *)(CPRMAN_BASE + 0x098UL))

// Every write to a CM_xxxCTL/CM_xxxDIV register must have this in bits[31:24]
// or the write is silently ignored by hardware.
#define CM_PASSWORD          0x5A000000U

// CTL — common to every CM_xxx clock generator on this SoC
#define CM_CTL_SRC_MASK       0xFU        // bits 3:0
#define CM_CTL_ENAB           (1U << 4)
#define CM_CTL_KILL           (1U << 5)
#define CM_CTL_BUSY           (1U << 7)   // read-only
#define CM_CTL_FLIP           (1U << 8)
#define CM_CTL_MASH_SHIFT     9           // bits 10:9

// DIV — DIVI.DIVF, DIVF is a 12-bit binary fraction (1/4096 units)
#define CM_DIV_FRAC_BITS      12U
#define CM_DIV_DIVI_MASK      0xFFFU      // bits 23:12
#define CM_DIV_DIVF_MASK      0xFFFU      // bits 11:0

// Only the oscillator source is supported for now — its rate is a fixed,
// known constant, so no mailbox query is needed. See docs.md's "Open items"
// for why PLLD_PER (a lower-jitter, higher-precision option for the 44.1kHz
// family) isn't wired up yet.
#define CPRMAN_CLK_SRC_OSCILLATOR 1U
#define CPRMAN_OSC_HZ             19200000U

// MASH stage — 1 gives fractional-divider accuracy (needed for standard audio
// rates, which don't divide evenly from a 19.2 MHz oscillator) without the
// higher instantaneous jitter of MASH 2/3. Not oscilloscope-verified — see docs.md.
#define CPRMAN_PCM_MASH_STAGE 1U

/**
 * @brief Enable the PCM clock generator at the closest achievable rate to target_hz.
 * @note Blocks briefly (busy-wait, not task_delay) while the generator settles — see docs.md.
 * @return E_INVALID_ARGS if target_hz doesn't fit the 12-bit integer divider
 * range from the oscillator; E_TIMED_OUT if BUSY never sets/clears as expected.
 */
StatusCode cprman_pcm_clock_enable(uint32_t target_hz);

/**
 * @brief Stop the PCM clock generator (graceful ENAB clear, KILL as a timeout fallback).
 */
void cprman_pcm_clock_disable(void);

/**
 * @brief Query whether the PCM clock generator is currently running (CTL.BUSY).
 */
bool cprman_pcm_clock_is_running(void);

/**
 * @brief Debug aid: read back the PCM clock generator's raw CTL/DIV registers.
 * @note Confirms what's actually latched in hardware (rules out "the write
 * didn't take") — does NOT confirm a real signal is toggling at the
 * resulting rate on the PCM_CLK pin. See docs.md and i2s/docs.md.
 */
void cprman_pcm_clock_status(uint32_t *out_ctl, uint32_t *out_div);

#endif
