#include "stdint.h"
#include <stdbool.h>

#include "boot.h"
#include "boot_flags.h"
#include "dfu_receive.h"
#include "dfu_trigger.h"
#include "emmc.h"
#include "jtag.h"
#include "status.h"
#include "uart.h"
#include "watchdog.h"

#define NUM_RETRIES    10U

// DAP access control register
#define DEBUG_ROM_BASE 0xFF800000
#define DAP_BASE       0xFF820000


static void bootloader_init();
static void bootloader_recovery_window();
static StatusCode bootloader_execute();

static void bootloader_recovery_window()
{
  if (boot_flags.dfu_requested == DFU_REQUEST) {
    return;
  }

  uart_print("boot: recovery window open (100ms) - send DFU trigger to override\r\n");

  uint32_t frq, lo, hi;
  asm volatile ("mrc  p15, 0, %0, c14, c0, 0" : "=r" (frq));
  asm volatile ("mrrc p15, 0, %0, %1,  c14"   : "=r" (lo), "=r" (hi));
  uint64_t start = ((uint64_t)hi << 32) | lo;
  uint64_t ticks = (uint64_t)frq * 1000 / 1000;

  dfu_trigger_reset();

  while (1) {
    asm volatile ("mrrc p15, 0, %0, %1, c14" : "=r" (lo), "=r" (hi));
    if ((((uint64_t)hi << 32) | lo) - start >= ticks) {
      boot_flags.dfu_requested = DFU_REQUEST;
      uart_print("boot: recovery window closed\r\n");
      break;
    }
    uint8_t byte;
    if ((uart_rx_nonblocking(&byte) == E_OK) && dfu_trigger_feed(byte)) {
      uart_print("boot: DFU trigger received in recovery window\r\n");
      boot_flags.dfu_requested = DFU_REQUEST;
      boot_flags.reset_reason = RESET_REASON_SOFTWARE;
      dfu_trigger_reset();
      break;
    }
  }
}

static void bootloader_init()
{
  StatusCode status;
  uart_init(UART_BAUDRATE_115200);
  uart_print("\r\n\n-------------------Bootloader start, initializing components-------------------\r\n");
  uart_print("uart initialized\r\n");
  jtag_gpio_init();
  uart_print("jtag initialized\r\n");


  boot_flags_init();

  uart_print("boot flags initialized\r\n");

  status = emmc_init();
  if (status != E_OK) {
    uart_printf("emmc module initialization failed with exit code: (%d)\r\n", status);
  }
  else {
    uart_print("emmc module initialized\r\n");
  }

  STATUS_OK_OR_WARN(boot_init());
  uart_print("boot module initialized\r\n");
  STATUS_OK_OR_WARN(dfu_init());
  uart_print("dfu module initialized\r\n");
  uart_print("-------------------Done initializing components-------------------\r\n\n\n");


  uart_print("\r\n\n-------------------Checking boot flags and WDG metadata-------------------\r\n");

  STATUS_OK_OR_WARN(wdt_meta_read());
  uart_printf("wdt meta loaded - active app slot %s%s\r\n",
              APP_SLOT_LETTER(wdt_meta.active_app_slot),
              wdt_meta.app_slot_trial ? " (trial)" : "");

  // Revalidate the active slot on a cold boot; a rollback below also forces it.
  bool revalidate = (boot_flags.reset_reason == RESET_REASON_COLD);

  // ── A/B trial resolution: judge the slot the *previous* boot left on trial ──
  // A healthy app confirms (clears the trial flag) on its first boot, so the
  // counter only climbs for an app that never reaches a good state.
  if (wdt_meta.app_slot_trial) {
    wdt_meta.trial_boot_count++;
    if (wdt_meta.trial_boot_count >= APP_SLOT_TRIAL_MAX_ATTEMPTS) {
      uint32_t prev = APP_SLOT_OTHER(wdt_meta.active_app_slot);
      uart_printf("boot: trial slot %s unconfirmed after %u boots - rolling back to %s\r\n",
                  APP_SLOT_LETTER(wdt_meta.active_app_slot),
                  wdt_meta.trial_boot_count, APP_SLOT_LETTER(prev));
      wdt_meta.active_app_slot = prev;
      wdt_meta.app_slot_trial = 0U;
      wdt_meta.trial_boot_count = 0U;
      revalidate = true;
    }
    else {
      uart_printf("boot: active slot %s on trial, attempt %u/%u\r\n",
                  APP_SLOT_LETTER(wdt_meta.active_app_slot),
                  wdt_meta.trial_boot_count, APP_SLOT_TRIAL_MAX_ATTEMPTS);
    }
  }

  wdt_meta.wdt_reset_count++;
  uart_printf("boot: WDT reset #%u (tolerance=%d, policy=%u)\r\n",
              wdt_meta.wdt_reset_count,
              wdt_meta.wdt_reset_tolerance,
              wdt_meta.wdt_reset_policy);

  // ── Reset-tolerance policy (independent of the automatic A/B trial above) ──
  if ((wdt_meta.wdt_reset_tolerance >= 0)
      && ((int32_t)wdt_meta.wdt_reset_count > wdt_meta.wdt_reset_tolerance)) {
    if (wdt_meta.wdt_reset_policy == (uint32_t)WATCHDOG_RESET_POLICY_FORCE_UPDATE) {
      uart_print("boot: tolerance exceeded, forcing DFU\r\n");
      wdt_meta.wdt_reset_count = 0U;
      boot_flags.dfu_requested = DFU_REQUEST;
      boot_flags.reset_reason = RESET_REASON_SOFTWARE;
    }
    else if (wdt_meta.wdt_reset_policy == (uint32_t)WATCHDOG_RESET_POLICY_ROLLBACK) {
      uint32_t prev = APP_SLOT_OTHER(wdt_meta.active_app_slot);
      uart_printf("boot: tolerance exceeded, rolling back to slot %s\r\n",
                  APP_SLOT_LETTER(prev));
      wdt_meta.active_app_slot = prev;
      wdt_meta.app_slot_trial = 0U;
      wdt_meta.trial_boot_count = 0U;
      wdt_meta.wdt_reset_count = 0U;
      revalidate = true;
    }
  }

  uint32_t active_sector = APP_SLOT_TO_SECTOR(wdt_meta.active_app_slot);
  if (revalidate) {
    boot_flags.fw_crc_ok = (boot_validateApp(active_sector) == E_OK) ? 1U : 0U;
    uart_printf("boot: validated active slot %s -> %s\r\n",
                APP_SLOT_LETTER(wdt_meta.active_app_slot),
                boot_flags.fw_crc_ok ? "OK" : "INVALID");
  }
  else {
    uart_print("warm boot: trusting preserved boot flags\r\n");
  }

  STATUS_OK_OR_WARN(wdt_meta_write());
  uart_print("-------------------Done checking metadata-------------------\r\n");
}

static StatusCode bootloader_execute()
{
  StatusCode ret;
  bool did_dfu = false;

  uart_print("\r\n\n-------------------Bootloader executing-------------------\r\n");

  if (boot_flags.dfu_requested == DFU_REQUEST) {
    uart_print("DFU requested! entering DFU recv loop...\r\n");
    if (dfu_receive() != E_OK) {
      return E_ABORTED;   // failed: active slot unchanged
    }
    did_dfu = true;
  }
  else if (!boot_flags.fw_crc_ok) {
    uart_print("No valid app in active slot, entering DFU recv loop...\r\n");
    if (dfu_receive() != E_OK) {
      return E_ABORTED;
    }
    did_dfu = true;
  }
  else {
    uart_print("Valid app found, loading active slot...\r\n");
  }

  // On success dfu_receive() read-back-validated the new image and flipped the
  // active slot to it (now on trial), so just boot whatever slot is active.
  if (did_dfu) {
    wdt_meta.wdt_reset_count = 0U;
    STATUS_OK_OR_WARN(wdt_meta_write());
  }

  ret = boot_loadApp(APP_SLOT_TO_SECTOR(wdt_meta.active_app_slot));
  if (ret != E_OK) {
    return ret;
  }
  boot_jumpToApp();
  return E_OK;
}

void kmain(void)
{
  bootloader_init();
  // bootloader_recovery_window();

  for (uint32_t retries = NUM_RETRIES; retries > 0; retries--) {
    StatusCode ret = bootloader_execute();
    uart_printf("boot: attempt failed (%d), %u retries left\r\n", ret, retries - 1);
  }

  uart_print("boot: all retries exhausted, entering DFU recovery loop\r\n");
  while (1) {
    if (dfu_receive() == E_OK) {
      // dfu_receive() validated the image and flipped the active slot to it.
      wdt_meta.wdt_reset_count = 0U;
      STATUS_OK_OR_WARN(wdt_meta_write());
      boot_loadApp(APP_SLOT_TO_SECTOR(wdt_meta.active_app_slot));
      boot_jumpToApp();
    }
    uart_print("boot: DFU recovery failed, retrying\r\n");
  }
}
