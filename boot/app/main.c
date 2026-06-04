#include "boot.h"
#include "boot_flags.h"
#include "dfu.h"
#include "uart.h"

void kmain(void)
{
  uart_init(UART_BAUDRATE_115200);
  boot_flags_init();

  if (boot_flags.dfu_requested == DFU_REQUEST) {
    if (dfu_recieve() == E_OK) {
      boot_loadApp();
      boot_jumpToApp();
    }
  }
  else if (boot_flags.fw_crc_ok) {
    boot_loadApp();
    boot_jumpToApp();
  }
  else {
    if (dfu_recieve() == E_OK) {
      boot_loadApp();
      boot_jumpToApp();
    }
  }
}
