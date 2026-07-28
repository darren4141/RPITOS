@ Vector table (_start + 8 branches, 0x00-0x1F) is shared across all images.
.include "startup/vectors.s"

    @ DFU-support marker at image offset 0x20 (just past the 8-entry vector
    @ table). Proves this app was built with the standard rpitos startup, so
    @ the bootloader's dfu_receive will accept it. Reached only as data — the
    @ reset vector above branches over it to _reset_handler.
    @ KEEP IN SYNC with DFU_APP_MAGIC in source/boot/dfu_receive/dfu_receive.h.
    .word 0x44465521             @ 0x20  'D' 'F' 'U' '!'

_reset_handler:

    @ ---- Park secondary cores immediately ----
    mrc  p15, 0, r0, c0, c0, 5      @ read MPIDR
    and  r0, r0, #0x3                @ extract core ID (bits [1:0])
    cmp  r0, #0
    bne  _secondary_hang$            @ non-zero core → park

    @ ---- Drop from HYP mode to SVC mode if needed -------------------------
    @ BCM2711/CM4 firmware leaves the CPU in non-secure HYP mode (0x1A).
    @ MSR CPSR cannot change the mode bits from HYP — must use ERET instead.
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

    @ Allow NS EL1 to access the physical counter and physical timer registers.
    @ CNTHCTL[1] PL1PCEN  = 1 → enables CNTP_CTL / CNTP_CVAL / CNTP_TVAL at EL1
    @ CNTHCTL[0] PL1PCTEN = 1 → enables CNTPCT (counter read) at EL1
    @ Default is 0 — without this, mcr/mrrc to those regs trap as Undefined Instruction.
    @ Must be done here in HYP mode; CNTHCTL is not writable from EL1.
    mrc  p15, 4, r0, c14, c1, 0    @ read  CNTHCTL_EL2
    orr  r0, r0, #0x3              @ set PL1PCEN | PL1PCTEN
    mcr  p15, 4, r0, c14, c1, 0    @ write CNTHCTL_EL2

    eret                            @ exits HYP → SVC, jumps to _stack_setup

_stack_setup:

    @ ---- Disable D-cache and I-cache --------------------------------
    @ IMPORTANT: must be done here in SVC mode so mcr writes EL1's SCTLR.
    @ If done before the HYP exit (still in HYP mode), mcr would write
    @ HSCTLR instead, leaving the EL1 D-cache enabled by firmware.
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
    push {r0-r12, lr}            @ save r0–r12 AND lr_svc on task's SVC stack.
                                 @ lr_svc is live task state: GCC uses lr as the
                                 @ return address in leaf functions (bx lr) and as
                                 @ a scratch register elsewhere. If it isn't part
                                 @ of the context frame, the next task resumes
                                 @ with the previous task's lr — a leaf-function
                                 @ return then jumps into data (e.g. another
                                 @ task's stack) and traps as Undefined.


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
    @ p_task_control_block is now one slot per core (TaskControlBlock
    @ *p_task_control_block[SMP_MAX_CORES]) — each core only ever touches its
    @ own slot here, so index by MPIDR & 3, not a bare symbol load. Keep the
    @ core id in r5 (callee-saved per AAPCS) so it survives the two bl calls
    @ below — r0-r3 are caller-saved/scratch and WILL be clobbered by them,
    @ so &p_task_control_block[core_id] must be recomputed after, not reused.
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

_secondary_hang$:
    wfe
    b _secondary_hang$

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
