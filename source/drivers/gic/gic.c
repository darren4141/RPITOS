#include "gic.h"

#define GICD_BASE      0xFF841000
#define GICC_BASE      0xFF842000
#define ARM_LOCAL_BASE 0xFF800000

#define GICD_CTLR      (0x000 / 4)
#define GICD_ISENABLER(n)     ((0x100 + (n) * 4) / 4)
#define GICD_IPRIORITYR(n)    ((0x400 + (n) * 4) / 4)
#define GICD_ITARGETSR(n)     ((0x800 + (n) * 4) / 4)
#define GICD_ICFGR(n)         ((0xC00 + (n) * 4) / 4)

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

  // 7. Route nCNTPNSIRQ to Core 0 IRQ via the ARM Local controller, not the
  // GIC's own PPI 30 path — see docs.md for why.
  CORE_TIMER_IRQCNTL(0) |= (1 << 1);   // nCNTPNSIRQ → Core0 IRQ

  __asm__ volatile ("dsb sy" ::: "memory");
}

void gic_enable_spi(uint32_t intid, uint8_t priority)
{
  volatile uint32_t *gicd = (volatile uint32_t *)GICD_BASE;

  // Priority — one byte per INTID.
  uint32_t pri_reg = intid / 4;
  uint32_t pri_shift = (intid % 4) * 8;
  gicd[GICD_IPRIORITYR(pri_reg)] =
    (gicd[GICD_IPRIORITYR(pri_reg)] & ~(0xFFU << pri_shift)) | ((uint32_t)priority << pri_shift);

  // Target — one byte per INTID; route to core 0 (bit 0). RW for SPIs only.
  uint32_t tgt_reg = intid / 4;
  uint32_t tgt_shift = (intid % 4) * 8;
  gicd[GICD_ITARGETSR(tgt_reg)] =
    (gicd[GICD_ITARGETSR(tgt_reg)] & ~(0xFFU << tgt_shift)) | (0x01U << tgt_shift);

  // Trigger — two bits per INTID; 0b00 = level-sensitive (DMA INT is level high).
  uint32_t cfg_reg = intid / 16;
  uint32_t cfg_shift = (intid % 16) * 2;
  gicd[GICD_ICFGR(cfg_reg)] &= ~(0x3U << cfg_shift);

  // Enable — one bit per INTID.
  gicd[GICD_ISENABLER(intid / 32)] = (1U << (intid % 32));

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