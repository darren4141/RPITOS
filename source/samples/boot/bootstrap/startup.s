@ Vector table (_start + 8 branches) is shared across all images.
.include "startup/vectors.s"

_reset_handler:

    @ ---- Secondary cores take their own bring-up path, then park ----
    mrc  p15, 0, r0, c0, c0, 5
    and  r0, r0, #0x3
    cmp  r0, #0
    bne  _secondary_boot$        @ cores 1-3 → HYP exit, cache off, own stack, park

    @ ---- Drop from HYP mode to SVC mode if needed ----
    mrs  r0, cpsr
    and  r0, r0, #0x1F
    cmp  r0, #0x1A
    bne  _stack_setup

    mrs  r0, cpsr
    bic  r0, r0, #0x1F
    orr  r0, r0, #0x13              @ SVC mode
    orr  r0, r0, #0xC0              @ I=1, F=1
    msr  spsr_cxsf, r0

    adr  r0, _stack_setup
    msr  elr_hyp, r0

    @ Allow NS EL1 access to physical timer registers
    mrc  p15, 4, r0, c14, c1, 0
    orr  r0, r0, #0x3
    mcr  p15, 4, r0, c14, c1, 0

    eret

_stack_setup:

    @ ---- Disable D-cache, I-cache, alignment checking (must be in SVC mode) ----
    @ GPU firmware may leave SCTLR.A (bit 1) set; clear it so unaligned accesses
    @ are handled by the core rather than faulting.
    mrc  p15, 0, r0, c1, c0, 0
    bic  r0, r0, #(1 << 1)        @ A: alignment check off
    bic  r0, r0, #(1 << 2)        @ C: D-cache off
    bic  r0, r0, #(1 << 12)       @ I: I-cache off
    bic  r0, r0, #(1 << 13)       @ V: high vectors off (VBAR used instead)
    mcr  p15, 0, r0, c1, c0, 0
    dsb
    isb

    @ ---- Banked stacks ----
    msr cpsr_c, #0xD1
    ldr sp, =_fiq_stack_top

    msr cpsr_c, #0xD2
    ldr sp, =_irq_stack_top

    msr cpsr_c, #0xD7
    ldr sp, =_abt_stack_top

    msr cpsr_c, #0xDB
    ldr sp, =_und_stack_top

    msr cpsr_c, #0xDF
    ldr sp, =_sys_stack_top

    msr cpsr_c, #0xD3
    ldr sp, =_svc_stack_top

    @ ---- Zero .bss ----
    ldr r0, =_bss_start
    ldr r1, =_bss_end
    mov r2, #0
zero_bss$:
    cmp  r0, r1
    strlt r2, [r0], #4
    blt  zero_bss$

    @ ---- Set VBAR ----
    ldr r0, =_start
    mcr p15, 0, r0, c12, c0, 0
    isb

    @ ---- Kick secondary cores out of the firmware armstub spin loop ----
    @ On Pi/CM4 32-bit only core 0 is dispatched to kernel7l.img; cores 1-3 are
    @ held by the armstub watching mailbox 3 in the ARM-local peripherals. Write
    @ the address of _secondary_boot$ to each core's mailbox-3-set register and
    @ SEV; the armstub then bx'es that core (in HYP) into _secondary_boot$, which
    @ does the HYP exit / cache disable / stack and parks in _sec_park$.
    @ Local base 0xFF800000; mailbox-3-set = base + 0x80 + core*0x10 + 3*4.
    ldr r0, =_secondary_boot$
    ldr r1, =0xFF80009C          @ core 1 mailbox 3 set
    str r0, [r1]
    ldr r1, =0xFF8000AC          @ core 2 mailbox 3 set
    str r0, [r1]
    ldr r1, =0xFF8000BC          @ core 3 mailbox 3 set
    str r0, [r1]
    dsb
    sev

    bl kmain

hang$:
    b hang$

@ Stub handlers — bootstrap does not use interrupts
_undef_handler:     b _undef_handler
_svc_handler:       b _svc_handler
_prefetch_handler:  b _prefetch_handler

@ Data abort: print fault PC, DFAR, and DFSR then hang.
@ ABT-mode stack is set up by _stack_setup so bl-to-C works.
@ AAPCS: r0=fmt, r1=faulting_PC, r2=DFAR, r3=DFSR
_data_handler:
    sub  r1, lr, #8                @ r1 = PC of faulting instruction (LR_abt = PC+8)
    mrc  p15, 0, r2, c6, c0, 0    @ r2 = DFAR
    mrc  p15, 0, r3, c5, c0, 0    @ r3 = DFSR
    adr  r0, _data_abort_fmt       @ r0 = format string (PC-relative, no literal pool)
    bl   uart_printf
_data_hang$:
    b    _data_hang$

@ Format string inline — adr above reaches here within a few bytes.
@ .align 2 keeps the next code label on a 4-byte boundary.
_data_abort_fmt:
    .asciz "EXCEPTION: data abort  PC=0x%08X  DFAR=0x%08X  DFSR=0x%08X\r\n"
    .align 2

_reserved_handler:  b _reserved_handler
_irq_handler:       b _irq_handler
_fiq_handler:       b _fiq_handler

@ ── Secondary core bring-up (cores 1-3) ─────────────────────────────────────
@ Bring each secondary to the same known state core 0 reaches — SVC mode, caches
@ off, its own stack — then park it watching the release mailbox. Core 0's path
@ above is left untouched. A secondary is released by the app writing its entry
@ address into mailbox[coreid] (see smp_start_core); it then runs app code on the
@ bootstrap-resident per-core stack set up here.
_secondary_boot$:

    @ ---- HYP -> SVC (secondaries also boot in HYP) ----
    mrs  r0, cpsr
    and  r0, r0, #0x1F
    cmp  r0, #0x1A
    bne  _sec_post_hyp$          @ not in HYP — skip
    mrs  r0, cpsr
    bic  r0, r0, #0x1F
    orr  r0, r0, #0x13           @ SVC mode
    orr  r0, r0, #0xC0           @ I=1, F=1
    msr  spsr_cxsf, r0
    adr  r0, _sec_post_hyp$
    msr  elr_hyp, r0
    mrc  p15, 4, r0, c14, c1, 0  @ CNTHCTL_EL2
    orr  r0, r0, #0x3            @ PL1PCEN | PL1PCTEN — timer-capable later
    mcr  p15, 4, r0, c14, c1, 0
    eret

_sec_post_hyp$:
    @ ---- Disable D/I-cache (must be SVC so this writes SCTLR, not HSCTLR) ----
    @ Without this the core's D-cache is on (firmware default) and GPIO MMIO
    @ writes never reach the peripheral — the usual "secondary does nothing".
    mrc  p15, 0, r0, c1, c0, 0
    bic  r0, r0, #(1 << 2)       @ C: D-cache off
    bic  r0, r0, #(1 << 12)      @ I: I-cache off
    bic  r0, r0, #(1 << 13)      @ V: VBAR-relative vectors
    mcr  p15, 0, r0, c1, c0, 0
    dsb
    isb

    @ ---- Per-core banked stacks + VBAR so this core can handle/report faults --
    @ Without a VBAR and an ABT stack, any exception on this core (e.g. a data
    @ abort in the app entry) vectors to a garbage address and hangs silently.
    @ Per-core block = 0x800: SVC (top 0x400) + ABT (next 0x400).
    @ base = _sec_stack_top - (coreid-1)*0x800.  r0 (coreid) preserved throughout.
    mrc  p15, 0, r0, c0, c0, 5   @ MPIDR
    and  r0, r0, #0x3            @ coreid (1..3)
    sub  r1, r0, #1
    lsl  r1, r1, #11            @ (coreid-1) * 0x800
    ldr  r3, =_sec_stack_top
    sub  r3, r3, r1             @ r3 = this core's stack top

    msr  cpsr_c, #0xD7          @ ABT mode (I=1,F=1)
    sub  sp, r3, #0x400
    msr  cpsr_c, #0xD3          @ back to SVC mode
    mov  sp, r3

    @ VBAR -> bootstrap vector table (_start, 0x8000): resident, and its data
    @ abort handler prints PC/DFAR/DFSR over UART instead of hanging.
    ldr  r2, =_start
    mcr  p15, 0, r2, c12, c0, 0
    isb

    @ ---- Park watching the release mailbox ----
    @ mailbox[coreid]: 0 = stay parked, else = entry address to blx.
    @ KEEP THE ADDRESS in sync with CORE_MAILBOX_ADDR in memory_map.h.
    ldr  r1, =0x88300            @ CORE_MAILBOX_ADDR
    add  r1, r1, r0, lsl #2      @ &mailbox[coreid]

    @ ── DIAGNOSTIC: blink GPIO16 three times on entry to the park loop. ──────
    @ Proves this core got through _secondary_boot$ (HYP exit, cache disable,
    @ stack) and reached the park loop. Runs once, then falls into the busy-poll.
    @ r0 = coreid, r1 = &mailbox[coreid] (both preserved). Delay is a register
    @ loop; ~0x100000 ≈ the 200000 volatile-loop feel with caches off — tune it.
    ldr  r3, =0xFE200000         @ GPIO base (BCM2711)
    ldr  r4, [r3, #0x04]         @ GPFSEL1 (pins 10-19)
    bic  r4, r4, #(7 << 18)      @ clear GPIO16 function field [20:18]
    orr  r4, r4, #(1 << 18)      @ GPIO16 = output
    str  r4, [r3, #0x04]
    mov  r5, #(1 << 16)          @ GPIO16 bit mask
    mov  r7, #3                  @ blink count
_sec_blink$:
    str  r5, [r3, #0x1C]         @ GPSET0 — LED on
    ldr  r6, =0x00100000
_sec_blink_on$:
    subs r6, r6, #1
    bne  _sec_blink_on$
    str  r5, [r3, #0x28]         @ GPCLR0 — LED off
    ldr  r6, =0x00100000
_sec_blink_off$:
    subs r6, r6, #1
    bne  _sec_blink_off$
    subs r7, r7, #1
    bne  _sec_blink$

_sec_park$:
    @ Busy-poll rather than WFE: cross-core SEV wakeups aren't reliably reaching
    @ this core in our post-armstub state. Caches are off, so the read is coherent.
    ldr  r2, [r1]
    cmp  r2, #0
    beq  _sec_park$              @ still parked
    blx  r2                      @ run the entry the app assigned
    b    _sec_park$              @ if it ever returns, re-park
