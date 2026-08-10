@ Vector table (_start + 8 branches, 0x00-0x1F) is shared across all images.
.include "startup/vectors.s"

    @ DFU-support marker, reached only as data (see docs.md).
    @ KEEP IN SYNC with DFU_APP_MAGIC in source/boot/dfu_receive/dfu_receive.h.
    .word 0x44465521             @ 0x20  'D' 'F' 'U' '!'

_reset_handler:

    @ ---- Park secondary cores immediately ----
    mrc  p15, 0, r0, c0, c0, 5      @ read MPIDR
    and  r0, r0, #0x3                @ extract core ID (bits [1:0])
    cmp  r0, #0
    bne  _secondary_hang$            @ non-zero core → park

    @ ---- Drop from HYP mode to SVC mode if needed (see docs.md) -----------
    mrs  r0, cpsr
    and  r0, r0, #0x1F              @ extract mode bits
    cmp  r0, #0x1A                  @ in HYP mode?
    bne  _stack_setup               @ no — already SVC/other, skip

    @ Build target CPSR: SVC mode, IRQ+FIQ masked
    mrs  r0, cpsr
    bic  r0, r0, #0x1F
    orr  r0, r0, #0x13              @ SVC mode
    orr  r0, r0, #0xC0              @ I=1, F=1
    msr  spsr_cxsf, r0              @ SPSR_hyp = target CPSR after eret

    @ Point ELR_hyp at _stack_setup so ERET continues there in SVC mode
    adr  r0, _stack_setup
    msr  elr_hyp, r0

    @ Allow NS EL1 to access the physical counter/timer regs — must be done here
    @ in HYP mode, CNTHCTL is not writable from EL1 (see docs.md).
    mrc  p15, 4, r0, c14, c1, 0    @ read  CNTHCTL_EL2
    orr  r0, r0, #0x3              @ set PL1PCEN | PL1PCTEN
    mcr  p15, 4, r0, c14, c1, 0    @ write CNTHCTL_EL2

    eret                            @ exits HYP → SVC, jumps to _stack_setup

_stack_setup:

    @ ---- Disable D-cache and I-cache --------------------------------
    @ Must be here in SVC mode: in HYP mode this mcr would write HSCTLR
    @ instead of SCTLR, leaving the EL1 D-cache enabled (see docs.md).
    mrc  p15, 0, r0, c1, c0, 0     @ read SCTLR
    bic  r0, r0, #(1 << 2)          @ C = 0  (D-cache off)
    bic  r0, r0, #(1 << 12)         @ I = 0  (I-cache off)
    bic  r0, r0, #(1 << 13)         @ V = 0  (use VBAR not 0xFFFF0000)
    mcr  p15, 0, r0, c1, c0, 0     @ write SCTLR
    dsb
    isb

    @ ---- Set up banked stacks for all modes (IRQs masked throughout) ----
    msr cpsr_c, #0xD1            @ FIQ mode, I=1, F=1
    ldr sp, =_fiq_stack_top

    msr cpsr_c, #0xD2            @ IRQ mode, I=1, F=1
    ldr sp, =_irq_stack_top

    msr cpsr_c, #0xD7            @ ABT mode, I=1, F=1
    ldr sp, =_abt_stack_top

    msr cpsr_c, #0xDB            @ UND mode, I=1, F=1
    ldr sp, =_und_stack_top

    msr cpsr_c, #0xDF            @ SYS mode, I=1, F=1
    ldr sp, =_sys_stack_top

    msr cpsr_c, #0xD3            @ SVC mode, I=1, F=1
    ldr sp, =_svc_stack_top

    @ Zero .bss
    ldr r0, =_bss_start
    ldr r1, =_bss_end
    mov r2, #0
zero_bss$:
    cmp  r0, r1
    strlt r2, [r0], #4
    blt  zero_bss$

    @ Set VBAR to our vector table
    ldr r0, =_start
    mcr p15, 0, r0, c12, c0, 0
    isb

    bl kmain

hang$:
    b hang$

@ Fault handlers — dump PC + fault address/status over UART, then hang.
@ AAPCS: r0=kind, r1=faulting PC, r2=fault addr, r3=fault status.
_undef_handler:
    mov  r0, #0
    sub  r1, lr, #4               @ approx faulting PC (ARM)
    mov  r2, #0
    mov  r3, #0
    bl   uart_fault_report
_undef_hang$:       b _undef_hang$

_svc_handler:       b _svc_handler

_prefetch_handler:
    mov  r0, #1
    sub  r1, lr, #4               @ LR_abt = faulting PC + 4
    mrc  p15, 0, r2, c6, c0, 2    @ IFAR
    mrc  p15, 0, r3, c5, c0, 1    @ IFSR
    bl   uart_fault_report
_pref_hang$:        b _pref_hang$

_data_handler:
    mov  r0, #2
    sub  r1, lr, #8               @ LR_abt = faulting PC + 8
    mrc  p15, 0, r2, c6, c0, 0    @ DFAR
    mrc  p15, 0, r3, c5, c0, 0    @ DFSR
    bl   uart_fault_report
_data_hang$:        b _data_hang$

_reserved_handler:  b _reserved_handler
_fiq_handler:       b _fiq_handler

_irq_handler:
    sub lr, lr, #4

    @ Push {lr_irq, SPSR_irq} to the SVC stack (sp_svc) while in IRQ mode
    srsdb sp!, #0x13             @ sp_svc -= 8; [sp_svc] = lr_irq, [sp_svc+4] = SPSR_irq

    cps  #0x13                   @ switch to SVC mode (sp = sp_svc = task's own stack)
    push {r0-r12, lr}            @ save r0-r12 AND lr_svc — lr_svc is live task
                                 @ state (GCC uses it as a leaf-function return
                                 @ address / scratch), must survive the switch (see docs.md)


    @ Check this core's ARM Local mailbox 0 BEFORE the GIC IAR read — the
    @ companion-core IPI bypasses the GIC entirely and would never show up
    @ via GICC_IAR (see docs.md / gic/docs.md).
    mrc  p15, 0, r5, c0, c0, 5   @ MPIDR
    and  r5, r5, #0x3            @ this core's id — kept in r5 (callee-saved) for
                                 @ mailbox_park_request$ below; cntx_switch$ recomputes its own copy
    ldr  r0, =0xFF800060          @ ARM_LOCAL_BASE + Core0 IRQ Source
    ldr  r1, [r0, r5, lsl #2]     @ this core's IRQ Source register
    tst  r1, #0x10                 @ LOCAL_IRQ_MAILBOX0 (bit 4)
    bne  mailbox_park_request$

    ldr r0, =0xFF842000     @ GICC base addr
    ldr r1, [r0, #0x0C]     @ GICC_IAR ACK, get id
    mov  r4, r1                  @ save IAR in callee-saved register

    @ Extract and check interrupt ID (bits [9:0])
    ldr  r3, =0x3FF             @ load mask into register
    and  r2, r1, r3             @ r2 = interrupt ID
    cmp  r2, r3                 @ ID == 0x3FF? spurious
    beq  irq_done$

    @ GIC ID 30 = nCNTPNSIRQ (EL1 non-secure physical timer). The context switch
    @ stays in asm — it saves/restores SP directly and can't go through C.
    cmp r2, #30
    beq cntx_switch$

    @ Every other INTID goes through the C dispatch table (irq_register). DMA TX
    @ (119) and UART RX (153) are registered there at driver init.
    cps  #0x12                   @ IRQ mode — run the C handler on the IRQ stack
    mov  r0, r2                  @ r0 = intid (r0-r7 not banked, survives the cps)
    bl   irq_dispatch
    cps  #0x13                   @ back to SVC mode
    b    irq_eoi$

cntx_switch$:
    @ p_task_control_block is one slot per core — index by MPIDR & 3, keep the
    @ core id in r5 (callee-saved) across the two bl calls below (see docs.md).
    mrc  p15, 0, r5, c0, c0, 5   @ MPIDR
    and  r5, r5, #0x3            @ this core's id

    ldr  r0, =p_task_control_block
    add  r0, r0, r5, lsl #2      @ &p_task_control_block[core_id]
    ldr  r1, [r0]
    str sp, [r1, #0]    @ Save task A's SP


    @ Use IRQ stack for C handlers (keeps task's SVC stack clean)
    cps  #0x12                   @ switch to IRQ mode
    bl timer_tick_handler
    bl scheduler_switch_context

    cps  #0x13                   @ back to SVC mode
    ldr  r0, =p_task_control_block
    add  r0, r0, r5, lsl #2      @ &p_task_control_block[core_id] — recomputed;
                                 @ r0 is scratch and may have changed above
    ldr  r1, [r0]
    ldr sp, [r1, #0]    @ Load task B's SP

irq_eoi$:
    @ Signal end of interrupt — write full IAR value to GICC_EOIR
    ldr  r0, =0xFF842000
    str  r4, [r0, #0x10]         @ GICC_EOIR — end of interrupt

irq_done$:
    pop  {r0-r12, lr}            @ restore r0–r12 and the task's own lr_svc
    rfeia sp!

mailbox_park_request$:
    @ DEBUG: raw marker "[M<core>]", see startup/docs.md
    ldr  r0, =0xFE201000          @ UART0 base
_dbg_mbox_wait1$:
    ldr  r1, [r0, #0x18]
    tst  r1, #0x20
    bne  _dbg_mbox_wait1$
    mov  r1, #'['
    str  r1, [r0, #0x00]
_dbg_mbox_wait2$:
    ldr  r1, [r0, #0x18]
    tst  r1, #0x20
    bne  _dbg_mbox_wait2$
    mov  r1, #'M'
    str  r1, [r0, #0x00]
_dbg_mbox_wait3$:
    ldr  r1, [r0, #0x18]
    tst  r1, #0x20
    bne  _dbg_mbox_wait3$
    add  r1, r5, #'0'
    str  r1, [r0, #0x00]
_dbg_mbox_wait4$:
    ldr  r1, [r0, #0x18]
    tst  r1, #0x20
    bne  _dbg_mbox_wait4$
    mov  r1, #']'
    str  r1, [r0, #0x00]

    @ Drain/ack this core's Mailbox 0 — read-to-clear, no GIC IAR/EOIR involved (see docs.md)
    ldr  r0, =0xFF8000C0           @ ARM_LOCAL_BASE + Core0 Mailbox0 RDCLR
    ldr  r2, [r0, r5, lsl #4]      @ read Core<id> Mailbox0 RDCLR, clears it

    @ No SP fixup needed — _companion_core_park$ switches to a dedicated stack immediately
    b    _companion_core_park$

_secondary_hang$:
    wfe
    b _secondary_hang$

@ Reached only via an ARM Local mailbox 0 IPI, requesting this core re-park
@ for a DFU/software reboot without a physical power cycle. Switches to a
@ dedicated per-core stack first — reusing the abandoned task's own stack is
@ unsafe (see docs.md). See docs.md for the full companion-core park protocol.
_companion_core_park$:
    mrc  p15, 0, r0, c0, c0, 5   @ MPIDR
    and  r0, r0, #0x3            @ this core's id

    ldr  r3, =g_companion_core_park_stack
    add  r4, r0, #1               @ (core id + 1)
    lsl  r4, r4, #13               @ * 8192 bytes (2048 words per core —
                                   @ KEEP IN SYNC with COMPANION_CORE_PARK_STACK_WORDS
                                   @ in companion_core.c)
    add  sp, r3, r4                @ sp = top of this core's dedicated park stack
    ldr  r1, =0x88300            @ CORE_MAILBOX_ADDR — KEEP IN SYNC with memory_map.h
    mov  r2, #0
    str  r2, [r1, r0, lsl #2]    @ zero our own mailbox slot — otherwise the
                                 @ wait loop below would immediately re-blx the
                                 @ stale old entry point instead of waiting
    dsb  sy

_companion_core_park_wait$:
    wfe
    ldr  r2, [r1, r0, lsl #2]
    cmp  r2, #0
    beq  _companion_core_park_wait$

    @ DEBUG: raw marker "[W<core>]", see startup/docs.md. r0/r1/r2 must
    @ survive; uses r6-r7 as scratch.
    ldr  r6, =0xFE201000
_dbg_wpark_wait1$:
    ldr  r7, [r6, #0x18]
    tst  r7, #0x20
    bne  _dbg_wpark_wait1$
    mov  r7, #'['
    str  r7, [r6, #0x00]
_dbg_wpark_wait2$:
    ldr  r7, [r6, #0x18]
    tst  r7, #0x20
    bne  _dbg_wpark_wait2$
    mov  r7, #'W'
    str  r7, [r6, #0x00]
_dbg_wpark_wait3$:
    ldr  r7, [r6, #0x18]
    tst  r7, #0x20
    bne  _dbg_wpark_wait3$
    add  r7, r0, #'0'
    str  r7, [r6, #0x00]
_dbg_wpark_wait4$:
    ldr  r7, [r6, #0x18]
    tst  r7, #0x20
    bne  _dbg_wpark_wait4$
    mov  r7, #']'
    str  r7, [r6, #0x00]

    blx  r2                      @ run whatever companion_core_start() published

    @ If it ever returns, go back to _companion_core_park$, NOT straight to
    @ _companion_core_park_wait$ — the mailbox slot is still stale (see docs.md).
    b    _companion_core_park$

.globl start_first_task
start_first_task:
    @ Called once per core, from that core's own scheduler_start(). Index
    @ p_task_control_block by this core's id — see cntx_switch$ above.
    mrc     p15, 0, r0, c0, c0, 5    @ MPIDR
    and     r0, r0, #0x3             @ this core's id
    ldr     r2, =p_task_control_block
    add     r2, r2, r0, lsl #2       @ &p_task_control_block[core_id]
    ldr     r1, [r2]                 @ R1 = first TCB
    ldr     sp, [r1, #0]            @ SP = current_sp (first field of TaskControlBlock)

    pop     {r0-r12, lr}        @ frame layout: [r0-r12][lr][pc][spsr]
    rfeia   sp!
