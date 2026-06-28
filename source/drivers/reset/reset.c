#include "reset.h"

#include <stdint.h>

#include "gentimer.h"
#include "gic.h"
#include "gpio.h"
#include "memory_map.h"
#include "uart.h"
#include "watchdog.h"

// Linker symbols from kernel.ld — bound the app's per-mode stacks
// (.stacks: FIQ, IRQ, ABT, UND, SYS, SVC, in that order, contiguous).
extern uint32_t _fiq_stack_bottom;
extern uint32_t _svc_stack_top;

void system_hard_reset(void)
{
    watchdog_trigger_reset();
}

void enter_bootloader(void)
{
    // Mask interrupts before tearing down the very peripherals that raise them.
    __asm__ volatile ("cpsid if" ::: "memory");

    gentimer_disable();
    gic_disable();
    gpio_off(16);

    // Zero the app's stacks. Not required for correctness — the bootloader's
    // own startup overwrites every banked SP before it's ever read, and a
    // freshly flashed app will overwrite this whole region anyway — but
    // avoids leaving stale stack contents lying around.
    for (uint32_t *p = &_fiq_stack_bottom; p < &_svc_stack_top; p++) {
        *p = 0;
    }

    uart_deinit();

    __asm__ volatile ("dsb sy" ::: "memory");
    __asm__ volatile ("isb" ::: "memory");

    void (*bootEntry)(void) = (void (*)(void)) BOOTLOADER_START_ADDR;
    bootEntry();
}
