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

    @ ---- Disable D-cache and I-cache (must be in SVC mode) ----
    mrc  p15, 0, r0, c1, c0, 0
    bic  r0, r0, #(1 << 2)
    bic  r0, r0, #(1 << 12)
    bic  r0, r0, #(1 << 13)
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
_data_handler:      b _data_handler
_reserved_handler:  b _reserved_handler
_irq_handler:       b _irq_handler
_fiq_handler:       b _fiq_handler

_secondary_hang$:
    wfe
    b _secondary_hang$
