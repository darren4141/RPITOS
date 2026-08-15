#include "cprman.h"

#include <stddef.h>

// ── Divisor computation ───────────────────────────────────────────────────────

// Fixed-point DIVI.DIVF in the register's own 12-bit-fraction format — no FPU
// use (see CLAUDE.md's "No FPU state saving unless explicitly enabled").
static StatusCode cprman_pcm_divisor(uint32_t target_hz, uint32_t *out_divi, uint32_t *out_divf)
{
  if (target_hz == 0) {
    return E_INVALID_ARGS;
  }

  uint64_t scaled = ((uint64_t)CPRMAN_OSC_HZ << CM_DIV_FRAC_BITS) / target_hz;
  uint32_t divi = (uint32_t)(scaled >> CM_DIV_FRAC_BITS);
  uint32_t divf = (uint32_t)(scaled & CM_DIV_DIVF_MASK);

  // DIVI < 2 is out of range for any MASH stage (datasheet minimum divisor).
  if (divi < 2U || divi > CM_DIV_DIVI_MASK) {
    return E_INVALID_ARGS;
  }

  *out_divi = divi;
  *out_divf = divf;
  return E_OK;
}

bool cprman_pcm_clock_is_running(void)
{
  return (CPRMAN_PCM->CTL & CM_CTL_BUSY) != 0;
}

void cprman_pcm_clock_status(uint32_t *out_ctl, uint32_t *out_div)
{
  if (out_ctl != NULL) {
    *out_ctl = CPRMAN_PCM->CTL;
  }
  if (out_div != NULL) {
    *out_div = CPRMAN_PCM->DIV;
  }
}

void cprman_pcm_clock_disable(void)
{
  CprmanClockRegs *regs = CPRMAN_PCM;

  regs->CTL = CM_PASSWORD | CPRMAN_CLK_SRC_OSCILLATOR;   // ENAB=0, KILL=0 — graceful stop

  uint32_t timeout = 100000;   // arbitrary — not calibrated against real settle timing
  while ((regs->CTL & CM_CTL_BUSY) && timeout > 0) {
    timeout--;
  }

  if (regs->CTL & CM_CTL_BUSY) {
    // Graceful stop didn't take — force it. KILL immediately stops the
    // generator mid-cycle, which can glitch a downstream peripheral still
    // clocked from it; only used as a last resort here.
    regs->CTL = CM_PASSWORD | CM_CTL_KILL;
    while (regs->CTL & CM_CTL_BUSY) {}
  }
}

StatusCode cprman_pcm_clock_enable(uint32_t target_hz)
{
  uint32_t divi, divf;
  StatusCode ret = cprman_pcm_divisor(target_hz, &divi, &divf);
  if (ret != E_OK) {
    return ret;
  }

  CprmanClockRegs *regs = CPRMAN_PCM;

  // Must be disabled before DIV/CTL are reprogrammed — the datasheet-standard
  // sequence for every CM_xxx clock generator on this SoC.
  cprman_pcm_clock_disable();

  regs->DIV = CM_PASSWORD | (divi << CM_DIV_FRAC_BITS) | divf;

  regs->CTL = CM_PASSWORD | (CPRMAN_PCM_MASH_STAGE << CM_CTL_MASH_SHIFT) | CPRMAN_CLK_SRC_OSCILLATOR;

  regs->CTL = CM_PASSWORD | (CPRMAN_PCM_MASH_STAGE << CM_CTL_MASH_SHIFT) | CPRMAN_CLK_SRC_OSCILLATOR | CM_CTL_ENAB;

  uint32_t timeout = 100000;   // arbitrary — not calibrated against real settle timing
  while (!(regs->CTL & CM_CTL_BUSY) && timeout > 0) {
    timeout--;
  }
  if (timeout == 0) {
    return E_TIMED_OUT;
  }

  return E_OK;
}
