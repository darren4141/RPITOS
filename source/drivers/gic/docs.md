# gic

## Why the EL1 physical timer is routed via ARM Local, not a GIC PPI

`gic_init()` routes the EL1 physical timer (PPI 30 / nCNTPNSIRQ) to core 0
through the ARM Local controller (`CORE_TIMER_IRQCNTL`) rather than relying
on the GIC distributor's PPI 30 enable alone.

The ARM Local path bypasses the GIC security model. Using GIC PPI 30 alone
requires PPI 30 to be in Group 1 (non-secure) in `GICD_IGROUPR`, which only
secure firmware can guarantee. If it's Group 0 the GIC silently ignores
non-secure `ISENABLER` writes and the interrupt never arrives. The ARM Local
path has no such restriction.

Consequence: the IRQ handler checks Core0 IRQ Source (`0xFF800060`) bit 1
instead of `GICC_IAR` to dispatch the timer. No `GICC_IAR`/`GICC_EOIR` is
needed for this path — the interrupt de-asserts automatically once
`CNTP_CVAL > CNTPCT`.
