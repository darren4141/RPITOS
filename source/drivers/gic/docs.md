# gic

## Global vs. per-core init

`gic_init()` used to do everything in one call, hardcoded to core 0. It's now
split in two, because most of what it touches is banked per-core on GIC-400
despite living at one nominal MMIO address:

- `gic_distributor_init()` — the one genuinely global piece (`GICD_CTLR`
  disable/re-enable). Call once, from any single core, before any core calls
  `gic_percore_init()`.
- `gic_percore_init()` — everything else: this core's `GICD_ISENABLER0`/
  `GICD_IPRIORITYR` bits for the PPI range, its `GICC_CTLR`/`GICC_PMR`, and
  its `CORE_TIMER_IRQCNTL(companion_core_id())` timer routing. **Every** core that
  wants to take IRQs — including core 0 — must call this itself; a core that
  skips it never receives an IRQ no matter what the other two did.

`gic_enable_spi()`/`gic_disable()` remain core-0-targeted / per-core
respectively — see their doc comments in `gic.h`.

## Why the EL1 physical timer is routed via ARM Local, not a GIC PPI

`gic_percore_init()` routes the EL1 physical timer (PPI 30 / nCNTPNSIRQ) to
the calling core through the ARM Local controller (`CORE_TIMER_IRQCNTL`)
rather than relying on the GIC distributor's PPI 30 enable alone.

The ARM Local path bypasses the GIC security model. Using GIC PPI 30 alone
requires PPI 30 to be in Group 1 (non-secure) in `GICD_IGROUPR`, which only
secure firmware can guarantee. If it's Group 0 the GIC silently ignores
non-secure `ISENABLER` writes and the interrupt never arrives. The ARM Local
path has no such restriction.

Consequence: the IRQ handler checks Core0 IRQ Source (`0xFF800060`) bit 1
instead of `GICC_IAR` to dispatch the timer. No `GICC_IAR`/`GICC_EOIR` is
needed for this path — the interrupt de-asserts automatically once
`CNTP_CVAL > CNTPCT`.
