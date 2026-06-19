#include "gic.h"

#define GICD_BASE      0xFF841000
#define GICC_BASE      0xFF842000
#define ARM_LOCAL_BASE 0xFF800000

#define GICD_CTLR      (0x000 / 4)
#define GICD_ISENABLER(n)     ((0x100 + (n) * 4) / 4)
#define GICD_IPRIORITYR(n)    ((0x400 + (n) * 4) / 4)

#define GICC_CTLR      (0x000 / 4)
#define GICC_PMR       (0x004 / 4)

#define CORE_TIMER_IRQCNTL(n) (*(volatile uint32_t *)(ARM_LOCAL_BASE + 0x40 + (n) * 4))

void gic_init(void)
{
  volatile uint32_t *gicd = (volatile uint32_t *)GICD_BASE;
  volatile uint32_t *gicc = (volatile uint32_t *)GICC_BASE;

  // 1. Disable distributor before configuration
  gicd[GICD_CTLR] = 0;
  __asm__ volatile ("dsb sy" ::: "memory");

  // 2. Enable PPI 30 (nCNTPNSIRQ — EL1 non-secure physical timer) in ISENABLER0
  gicd[GICD_ISENABLER(0)] |= (1U << 30);

  // 3. Set priority for PPI 30
  // register index = 30/4 = 7, byte = 30%4 = 2, shift = 16
  uint32_t pri_reg = 30 / 4;
  uint32_t pri_shift = (30 % 4) * 8;
  gicd[GICD_IPRIORITYR(pri_reg)] = (gicd[GICD_IPRIORITYR(pri_reg)] & ~(0xFFU << pri_shift)) | (0x80U << pri_shift);

  // 4. Re-enable distributor
  gicd[GICD_CTLR] = 1;

  // 5. Set CPU interface priority mask — allow all priorities
  gicc[GICC_PMR] = 0xFF;

  // 6. Enable CPU interface
  gicc[GICC_CTLR] = 1;

  // 7. Route nCNTPNSIRQ to Core 0 IRQ via the ARM Local controller.
  //
  // The ARM Local path BYPASSES the GIC security model.  Using GIC PPI 30
  // alone requires PPI 30 to be in Group 1 (non-secure) in GICD_IGROUPR,
  // which only secure firmware can guarantee.  If it's Group 0 the GIC
  // silently ignores non-secure ISENABLER writes and the interrupt never
  // arrives.  The ARM Local path has no such restriction.
  //
  // The IRQ handler checks Core0 IRQ Source (0xFF800060) bit 1 instead of
  // GICC_IAR to dispatch the timer.  No GICC_IAR/GICC_EOIR needed for this
  // path — the interrupt de-asserts automatically once CNTP_CVAL > CNTPCT.
  CORE_TIMER_IRQCNTL(0) |= (1 << 1);   // nCNTPNSIRQ → Core0 IRQ

  __asm__ volatile ("dsb sy" ::: "memory");
}

void gic_disable(void)
{
  volatile uint32_t *gicd = (volatile uint32_t *)GICD_BASE;
  volatile uint32_t *gicc = (volatile uint32_t *)GICC_BASE;

  gicc[GICC_CTLR] = 0;
  gicd[GICD_CTLR] = 0;

  CORE_TIMER_IRQCNTL(0) &= ~(1U << 1);   // undo nCNTPNSIRQ → Core0 IRQ routing

  __asm__ volatile ("dsb sy" ::: "memory");
}