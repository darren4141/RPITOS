#include <stdbool.h>

#include "boot.h"
#include "boot_flags.h"
#include "dfu.h"
#include "emmc.h"
#include "status.h"
#include "uart.h"

#define NUM_RETRIES 10U

static void bootloader_init()
{
  uart_init(UART_BAUDRATE_115200);
  boot_flags_init();
  STATUS_OK_OR_WARN(emmc_init());
  STATUS_OK_OR_WARN(boot_init());
  STATUS_OK_OR_WARN(dfu_init());
}

static void bootloader_execute()
{
  StatusCode ret;
  if (boot_flags.dfu_requested == DFU_REQUEST) {
    if (dfu_receive() == E_OK) {
      ret = boot_loadApp();
      if (ret != E_OK) {
        spin();
      }
      boot_jumpToApp();
    }
  }
  else if (boot_flags.fw_crc_ok || (boot_validateApp() == E_OK)) {
    ret = boot_loadApp();
    if (ret != E_OK) {
      spin();
    }
    boot_jumpToApp();
  }
  else {
    if (dfu_receive() == E_OK) {
      ret = boot_loadApp();
      if (ret != E_OK) {
        spin();
      }
      boot_jumpToApp();
    }
  }
}


static void spin()
{
  uint32_t retries = NUM_RETRIES;
  while (retries-- > 0) {
    dfu_init();
    bootloader_execute();
  }

  uart_print("boot: fatal error, halting\r\n");
  while (1) {
  }
}

void kmain(void)
{
  bootloader_init();
  bootloader_execute();
}
