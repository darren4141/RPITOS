# interrupts

Flat INTID → handler dispatch table, indexed directly by GIC interrupt ID
for O(1) dispatch from IRQ context. Covers SGIs (0-15), PPIs (16-31), and
SPIs (32+) up to `IRQ_MAX_INTID` — raise that constant if a driver needs to
register a higher-numbered INTID than it currently allows (it costs
`IRQ_MAX_INTID * 4` bytes of `.bss`).

`irq_register()` only populates the table — it does not touch the GIC.
Callers must separately call `gic_enable_spi()` (or enable the relevant PPI)
for the interrupt to actually reach the CPU.

`enter_critical()`/`exit_critical()` here are the codebase's only critical
section primitive: mask IRQs, save/restore CPSR. There is no additional
locking for SMP — see `smp/docs.md`.
