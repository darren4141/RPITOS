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
    boot_flags.fw_crc_ok = (boot_validateApp() == E_OK) ? 1U : 0U;
    uart_print("cold boot: re-validated app in eMMC\r\n");
  }
  else {
    uart_print("warm boot: trusting preserved boot flags\r\n");
  }
}

static StatusCode bootloader_execute()
{
  StatusCode ret;

  uart_print("Bootloader executing...\r\n");

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

  uart_print("boot: all retries exhausted, entering DFU recovery loop\r\n");
  while (1) {
    dfu_receive();
    StatusCode ret = boot_validateApp();
    if (ret == E_OK) {
      boot_flags.fw_crc_ok = 1;
      boot_loadApp();
      boot_jumpToApp();
    }
    uart_print("boot: DFU recovery failed, retrying\r\n");
  }
}
