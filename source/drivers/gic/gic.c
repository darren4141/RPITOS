#include "gic.h"

#include "companion_core.h"

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

// ARM Local per-core mailboxes — companion-core IPI signal, bypasses GIC entirely (see docs.md)
#define CORE_MBOX_IRQCNTL(n)  (*(volatile uint32_t *)(ARM_LOCAL_BASE + 0x50 + (n) * 4))
#define CORE_MBOX0_SET(n)     (*(volatile uint32_t *)(ARM_LOCAL_BASE + 0x80 + (n) * 0x10))

void gic_distributor_init(void)
{
  volatile uint32_t *gicd = (volatile uint32_t *)GICD_BASE;

  // Disable then re-enable — the only genuinely global (non-banked) register
  // this driver touches. Everything PPI/CPU-interface/timer-routing related
  // is per-core and lives in gic_percore_init() instead.
  gicd[GICD_CTLR] = 0;
  __asm__ volatile ("dsb sy" ::: "memory");
  gicd[GICD_CTLR] = 1;
  __asm__ volatile ("dsb sy" ::: "memory");
}

void gic_percore_init(void)
{
  volatile uint32_t *gicd = (volatile uint32_t *)GICD_BASE;
  volatile uint32_t *gicc = (volatile uint32_t *)GICC_BASE;

  // 1. Enable PPI 30 (nCNTPNSIRQ — EL1 non-secure physical timer) in this
  // core's banked ISENABLER0.
  gicd[GICD_ISENABLER(0)] |= (1U << 30);

  // 2. Set priority for PPI 30 in this core's banked IPRIORITYR.
  // register index = 30/4 = 7, byte = 30%4 = 2, shift = 16
  uint32_t pri_reg = 30 / 4;
  uint32_t pri_shift = (30 % 4) * 8;
  gicd[GICD_IPRIORITYR(pri_reg)] = (gicd[GICD_IPRIORITYR(pri_reg)] & ~(0xFFU << pri_shift)) | (0x80U << pri_shift);

  // 3. Set this core's CPU interface priority mask — allow all priorities
  gicc[GICC_PMR] = 0xFF;

  // 4. Enable this core's CPU interface
  gicc[GICC_CTLR] = 1;

  // 5. Route nCNTPNSIRQ to this core's IRQ via the ARM Local controller (one
  // distinct MMIO address per core), not the GIC's own PPI 30 path — see
  // docs.md for why.
  CORE_TIMER_IRQCNTL(companion_core_id()) |= (1 << 1);   // nCNTPNSIRQ → this core's IRQ

  // 6. Enable this core's ARM Local mailbox 0 IRQ — the companion-core IPI signal (see docs.md)
  CORE_MBOX_IRQCNTL(companion_core_id()) |= (1U << 0);

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

void gic_send_mailbox_ipi(uint32_t target_core)
{
  // Any nonzero value triggers the target core's mailbox 0 IRQ; value carries no meaning (see docs.md)
  CORE_MBOX0_SET(target_core) = 1U;

  __asm__ volatile ("dsb sy" ::: "memory");
}

void gic_disable(void)
{
  volatile uint32_t *gicc = (volatile uint32_t *)GICC_BASE;

  gicc[GICC_CTLR] = 0;                                   // this core's CPU interface only — GICD_CTLR is left alone (shared by every other core)

  CORE_TIMER_IRQCNTL(companion_core_id()) &= ~(1U << 1); // undo nCNTPNSIRQ → this core's IRQ routing
  CORE_MBOX_IRQCNTL(companion_core_id()) &= ~(1U << 0);  // undo mailbox 0 IRQ routing

  __asm__ volatile ("dsb sy" ::: "memory");
}