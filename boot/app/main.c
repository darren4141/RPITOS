#include "stdint.h"
#include <stdbool.h>

#include "boot.h"
#include "boot_flags.h"
#include "dfu.h"
#include "emmc.h"
#include "jtag.h"
#include "status.h"
#include "uart.h"

#define NUM_RETRIES    10U

// DAP access control register
#define DEBUG_ROM_BASE 0xFF800000
#define DAP_BASE       0xFF820000


static void bootloader_init();
static StatusCode bootloader_execute();

static void bootloader_init()
{
  StatusCode status;
  uart_init(UART_BAUDRATE_115200);
  uart_print("Bootloader start, initializing components\r\n");
  uart_print("uart initialized\r\n");
  jtag_gpio_init();
  uart_print("jtag initialized\r\n");
  boot_flags_init();
  uart_print("boot flags initialized\r\n");

  for (int i = 0; i < 16; i++) {
    uint32_t val = *(volatile uint32_t *)(0xFF800000 + i * 4);
    uart_printf("0xFF800000 + 0x%x = 0x%x\r\n", i * 4, val);
  }

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

  if (boot_flags.reset_reason == RESET_REASON_COLD) {
    // Cold path: boot_flags_init() couldn't find a valid magic word, which
    // means either this is a real power-on (RAM is volatile — guaranteed
    // not to retain anything) or a reset where we can't be sure RAM survived
    // (our own watchdog-triggered system_reset() doesn't cut power to RAM,
    // but it does re-run the GPU's closed-source boot stage, which re-inits
    // the SDRAM controller — whether that preserves existing content isn't
    // documented anywhere we can verify). Either way, dfu_requested/fw_crc_ok
    // were already zeroed by boot_flags_init() and must not be trusted, so
    // re-derive fw_crc_ok from scratch by checking what's actually in eMMC.
    //
    // STUB: boot_validateApp() always returns E_OK today. Replace with a
    // real CRC/signature check against the eMMC-resident app image
    // (EMMC_SECTOR_APP) before relying on this path in the field.
    boot_flags.fw_crc_ok = (boot_validateApp() == E_OK) ? 1U : 0U;
    uart_print("cold boot: re-validated app in eMMC\r\n");
  }
  else {
    // Warm path: the magic word survived, so we trust the rest of the
    // struct survived with it (single contiguous region, no per-field
    // retention mechanism). fw_crc_ok and dfu_requested are exactly as the
    // app or a previous bootloader run left them — bootloader_execute()
    // dispatches on them directly below, no re-validation needed.
    uart_print("warm boot: trusting preserved boot flags\r\n");
  }
}

// Returns E_OK only if boot_jumpToApp() is about to be called (never returns to caller).
// Returns an error code if this attempt failed; caller should retry or halt.
//
// fw_crc_ok is authoritative by the time this runs — bootloader_init() either
// freshly derived it (cold path) or left it as a trusted, preserved value
// (warm path) — so this function just dispatches on the flags as-is.
static StatusCode bootloader_execute()
{
  StatusCode ret;

  uart_print("Bootloader executing...\r\n");

  // Temp: force DFU request
  boot_flags.dfu_requested = DFU_REQUEST;

  if (boot_flags.dfu_requested == DFU_REQUEST) {
    uart_print("DFU requested! entering DFU recv loop...\r\n");
    if (dfu_receive() != E_OK) {
      return E_ABORTED;
    }
    ret = boot_loadApp();
    if (ret != E_OK) {
      return ret;
    }
    boot_jumpToApp();
  }
  else if (boot_flags.fw_crc_ok) {
    uart_print("Valid app found, loading app...\r\n");
    ret = boot_loadApp();
    if (ret != E_OK) {
      return ret;
    }
    boot_jumpToApp();
  }
  else {
    uart_print("No valid app found, entering DFU recv loop...\r\n");
    if (dfu_receive() != E_OK) {
      return E_ABORTED;
    }
    ret = boot_loadApp();
    if (ret != E_OK) {
      return ret;
    }
    boot_jumpToApp();
  }
  return E_OK;
}

void kmain(void)
{
  bootloader_init();

  for (uint32_t retries = NUM_RETRIES; retries > 0; retries--) {
    StatusCode ret = bootloader_execute();
    uart_printf("boot: attempt failed (%d), %u retries left\r\n", ret, retries - 1);
  }

  uart_print("boot: fatal error, halting\r\n");
  while (1) {}
}
