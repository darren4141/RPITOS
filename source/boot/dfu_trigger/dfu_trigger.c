#include "dfu_trigger.h"

static uint32_t shift_reg = 0;

void dfu_trigger_reset(void)
{
  shift_reg = 0;
}

int dfu_trigger_get_val()
{
  return shift_reg;
}

int dfu_trigger_feed(uint8_t byte)
{
  shift_reg = (shift_reg << 8) | byte;
  return shift_reg == DFU_TRIGGER_KEY;
}

// ── RTOS reboot path (not compiled into the minimal bootloader)
#ifndef UART_MINIMAL

#include <stddef.h>

#include "boot_flags.h"
#include "reset.h"
#include "semaphore.h"
#include "task.h"
#include "uart.h"

static Semaphore dfu_semaphore;
static TaskControlBlock *dfu_tcb = NULL;

// Runs in IRQ context (registered via uart_rx_irq_enable). Only signals — the
// actual reboot must happen in task context because enter_bootloader() zeros
// the banked stacks, which would wipe the IRQ stack out from under us here.
void dfu_trigger_feed_isr(uint8_t byte)
{
  if (dfu_trigger_feed(byte)) {
    semaphore_give(&dfu_semaphore);
  }
}

// Blocks with zero CPU cost until the trigger fires, then reboots into the
// bootloader from SVC/task context (runs on its own task stack, not .stacks).
static void dfu_reboot_task(void *params)
{
  (void)params;
  semaphore_take(&dfu_semaphore, SEMAPHORE_TAKE_BLOCKING);

  uart_print("DFU trigger received, rebooting to bootloader\r\n");
  __asm__ volatile ("cpsid i" ::: "memory");
  boot_flags.dfu_requested = DFU_REQUEST;
  boot_flags.reset_reason = RESET_REASON_SOFTWARE;
  boot_flags.magic = BOOT_FLAGS_MAGIC;
  enter_bootloader();
}

StatusCode dfu_trigger_task_start(void)
{
  dfu_trigger_reset();
  semaphore_init(&dfu_semaphore, 1, 0, "dfu_trigger_sem");
  return task_create(dfu_reboot_task, 1024, TASK_PRIORITY_5, NULL, "dfu_reboot", &dfu_tcb);
}

#endif
