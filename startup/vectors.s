@ ── Shared exception vector table ────────────────────────────────────────────
@ Included first in each image's .init, so _start lands at the image base and
@ becomes the value written to VBAR. Every includer (app, bootloader, bootstrap)
@ defines the eight handler labels these entries branch to: the TABLE is one
@ source of truth, the HANDLERS stay per-image.
@
@ Direct `b label` branches only — never `ldr pc, =label`. A vector table runs
@ before caches/MMU are configured; the literal-pool read that `ldr pc, =` emits
@ can silently fail there. `b` is a self-contained relative branch (±32 MB),
@ which reaches any handler in the image with no memory access.
.section .init
.globl _start
_start:
    b _reset_handler             @ 0x00 Reset
    b _undef_handler             @ 0x04 Undefined instruction
    b _svc_handler               @ 0x08 SVC (syscall)
    b _prefetch_handler          @ 0x0C Prefetch abort
    b _data_handler              @ 0x10 Data abort
    b _reserved_handler          @ 0x14 Reserved (unused)
    b _irq_handler               @ 0x18 IRQ
    b _fiq_handler               @ 0x1C FIQ
