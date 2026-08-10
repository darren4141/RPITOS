@ ── Shared exception vector table ────────────────────────────────────────────
@ .include'd first by every image's .init (app/bootloader/bootstrap), each of
@ which defines its own eight handler labels. Direct `b label` only — never
@ `ldr pc, =label`, which can silently fail this early (see docs.md).
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
