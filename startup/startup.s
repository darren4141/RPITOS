.section .init
.globl _start
_start:

    /* Set up stack — grows down from 0x80000 (below the kernel) */
    ldr sp, =0x8000

    /* Zero .bss */
    ldr r0, =_bss_start
    ldr r1, =_bss_end
    mov r2, #0
zero_bss$:
    cmp r0, r1
    strlt r2, [r0], #4
    blt zero_bss$

    /* Jump to C */
    bl kmain

    /* kmain should never return — hang if it does */
hang$:
    b hang$
