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
    @ Harmless if bootstrap already did this earlier in the same boot session —
    @ cores 1-3 have already left the armstub and are busy-polling the software
    @ mailbox instead, so a second armstub-mailbox write lands on nobody. This
    @ makes the bootloader self-sufficient for cases where it is the first code
    @ to run with the secondaries still trapped in the armstub (e.g. loaded
    @ directly over JTAG, bypassing bootstrap). See bootstrap/startup.s for the
    @ full explanation of this sequence.
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

@ Stub handlers — bootloader does not use interrupts
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
@ Own copy of bootstrap's per-core bring-up (see bootstrap/startup.s for the
@ full rationale) — bootloader cannot branch into bootstrap's local labels
@ (separately-linked image, no shared symbol table), so this is a deliberate
@ duplicate, not a shortcut. Brings each secondary to SVC mode, caches off,
@ its own stack, then parks it watching the same software mailbox bootstrap
@ uses, so smp_start_core() works no matter which image's kick actually ran.
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
    @ Without a VBAR and an ABT stack, any exception on this core vectors to a
    @ garbage address and hangs silently.
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

    @ VBAR -> bootloader's own vector table (_start, 0x10000): resident, and
    @ its data abort handler prints PC/DFAR/DFSR over UART instead of hanging.
    ldr  r2, =_start
    mcr  p15, 0, r2, c12, c0, 0
    isb

    @ ---- Park watching the release mailbox ----
    @ mailbox[coreid]: 0 = stay parked, else = entry address to blx.
    @ KEEP THE ADDRESS in sync with CORE_MAILBOX_ADDR in memory_map.h.
    ldr  r1, =0x88300            @ CORE_MAILBOX_ADDR
    add  r1, r1, r0, lsl #2      @ &mailbox[coreid]

_sec_park$:
    @ Busy-poll rather than WFE: cross-core SEV wakeups aren't reliably reaching
    @ this core in our post-armstub state. Caches are off, so the read is coherent.
    ldr  r2, [r1]
    cmp  r2, #0
    beq  _sec_park$              @ still parked
    blx  r2                      @ run the entry the app assigned
    b    _sec_park$              @ if it ever returns, re-park
