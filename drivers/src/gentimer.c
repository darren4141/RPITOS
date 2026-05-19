#include "gentimer.h"
#include <stdint.h>

static uint32_t *p_freq;

static void delay(uint32_t count)
{
  while (count--) {
    __asm__ volatile ("nop");
  }
}

/**
 * Uses the EL1 Physical timer, p15, c14
 */
void gentimer_init(uint32_t *clk_freq, uint32_t hz)
{

  // Read CNTFRQ as a sanity check but override with the known value.
  p_freq = clk_freq;
  __asm__ volatile ("mrc p15, 0, %0, c14, c0, 0" : "=r" (*p_freq)); // CNTFRQ
  *p_freq = 54000000;                                               // BCM2711: always 54 MHz regardless of CNTFRQ

  // Disable the timer
  uint32_t ctl = 0;
  __asm__ volatile ("mcr p15, 0, %0, c14, c2, 1" : : "r" (ctl));    // CNTP_CTL = 0

  // Read the compare value (CVAL)
  uint32_t lo;
  uint32_t hi;
  __asm__ volatile ("mrrc p15, 0, %0, %1, c14" : "=r" (lo), "=r" (hi));   // CNTPCT
  uint64_t current = ((uint64_t)hi << 32) | lo;

  // Write the compare value (CVAL) (1-step ahead)
  uint64_t cval = current + (*p_freq / hz);
  uint32_t cval_lo = (uint32_t)(cval & 0xFFFFFFFF);
  uint32_t cval_hi = (uint32_t)(cval >> 32);
  __asm__ volatile ("mcrr p15, 2, %0, %1, c14" : : "r" (cval_lo), "r" (cval_hi)); // CNTP_CVAL

  ctl = 0x1;                                                                      // bit0=ENABLE, bit1=IMASK(0=unmasked), bit2=ISTATUS(RO)
  __asm__ volatile ("mcr p15, 0, %0, c14, c2, 1" : : "r" (ctl));                  // CNTP_CTL
}
