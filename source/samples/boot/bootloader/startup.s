.section .init
.globl _start
_start:
    b _reset_handler             @ 0x00 Reset
    b _undef_handler             @ 0x04 Undefined instruction
    b _svc_handler               @ 0x08 SVC
    b _prefetch_handler          @ 0x0C Prefetch abort
    b _data_handler              @ 0x10 Data abort
    b _reserved_handler          @ 0x14 Reserved
    b _irq_handler               @ 0x18 IRQ
    b _fiq_handler               @ 0x1C FIQ

_reset_handler:

    @ ---- Park secondary cores immediately ----
    mrc  p15, 0, r0, c0, c0, 5
    and  r0, r0, #0x3
    cmp  r0, #0
    bne  _secondary_hang$

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

_secondary_hang$:
    wfe
    b _secondary_hang$
