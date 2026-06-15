#include "stdint.h"
#include <stdbool.h>

#include "boot.h"
#include "boot_flags.h"
#include "dfu.h"
#include "emmc.h"
#include "status.h"
#include "uart.h"

#define NUM_RETRIES 10U

static void bootloader_init();
static StatusCode bootloader_execute();

static void bootloader_init()
{
  StatusCode status;
  uart_init(UART_BAUDRATE_115200);
  uart_print("Bootloader start, initializing components\r\n");
  uart_print("uart initialized\r\n");
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
}

// Returns E_OK only if boot_jumpToApp() is about to be called (never returns to caller).
// Returns an error code if this attempt failed; caller should retry or halt.
static StatusCode bootloader_execute()
{
  StatusCode ret;
  if (boot_flags.dfu_requested == DFU_REQUEST) {
    if (dfu_receive() != E_OK) {
      return E_ABORTED;
    }
    ret = boot_loadApp();
    if (ret != E_OK) {
      return ret;
    }
    boot_jumpToApp();
  }
  else if (boot_flags.fw_crc_ok || (boot_validateApp() == E_OK)) {
    ret = boot_loadApp();
    if (ret != E_OK) {
      return ret;
    }
    boot_jumpToApp();
  }
  else {
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

  // TEMP: force CRC_OK flag to 1
  boot_flags.fw_crc_ok = 1;
  // TEMP: always enter DFU mode
  boot_flags.dfu_requested = DFU_REQUEST;

  for (uint32_t retries = NUM_RETRIES; retries > 0; retries--) {
    StatusCode ret = bootloader_execute();
    uart_printf("boot: attempt failed (%d), %u retries left\r\n", ret, retries - 1);
  }

  uart_print("boot: fatal error, halting\r\n");
  while (1) {}
}
