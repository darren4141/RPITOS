# gentimer

Wraps the ARM Generic Timer's EL1 physical timer (CNTP), accessed via
coprocessor `p15, c14` — not the virtual timer (CNTV). See
`CLAUDE.md`'s "Timer / Interrupt Setup" section for the full register table
and the GIC PPI wiring this pairs with.

`gentimer_init()` reads `CNTFRQ` as a sanity check but overrides it with the
known BCM2711 value (54 MHz) rather than trusting the register, since
`CNTFRQ` isn't always programmed correctly by firmware on this SoC.
