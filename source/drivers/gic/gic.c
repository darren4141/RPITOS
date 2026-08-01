#include "gic.h"

  << << << < HEAD
#include "smp.h"
== == == =
#include "companion_core.h"
  >> >> >> > 10d4a98(multicore improvements)

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

    << << << < HEAD
    == == == =
// ARM Local per-core mailboxes — a separate, non-GIC signal used for
// companion-core IPIs (see companion_core_soft_reset_plan.md's "Why mailbox,
// not SGI": GICD_IGROUPR is confirmed write-ignored from this Non-secure-only
// OS, so GIC SGIs can never be delivered here; these bypass the GIC entirely,
// same reasoning as CORE_TIMER_IRQCNTL above). Mailbox 0 specifically —
// matches Linux's own convention for IPIs; mailbox 3 is already used by the
// boot ROM/bootstrap's one-time secondary-core release kick (see
// bootstrap/startup.s), 1 and 2 are unused by anything in this codebase.
#define CORE_MBOX_IRQCNTL(n)  (*(volatile uint32_t *)(ARM_LOCAL_BASE + 0x50 + (n) * 4))
#define CORE_MBOX0_SET(n)     (*(volatile uint32_t *)(ARM_LOCAL_BASE + 0x80 + (n) * 0x10))

    >> >> >> > 10d4a98(multicore improvements)
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
    << << << < HEAD CORE_TIMER_IRQCNTL(smp_core_id()) |= (1 << 1); // nCNTPNSIRQ → this core's IRQ
  == == == =
    CORE_TIMER_IRQCNTL(companion_core_id()) |= (1 << 1);           // nCNTPNSIRQ → this core's IRQ

  // 6. Enable this core's ARM Local mailbox 0 IRQ — the companion-core IPI
  // signal (see the CORE_MBOX_IRQCNTL comment above). Bit 0 of this register
  // is mailbox 0's IRQ enable (bits [3:0] = mailboxes 0-3, mirroring
  // CORE_TIMER_IRQCNTL's IRQ/FIQ split above).
  CORE_MBOX_IRQCNTL(companion_core_id()) |= (1U << 0);
  >> >> >> > 10d4a98(multicore improvements)

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
  // Any nonzero value triggers the target core's mailbox 0 IRQ; the value
  // itself carries no meaning here (this mailbox is reserved for a single
  // purpose — see the CORE_MBOX_IRQCNTL comment). Cleared by the receiving
  // core reading its own Mailbox 0 RDCLR register (startup.s's
  // _irq_handler) — reading it clears it, there's no separate write-to-clear.
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