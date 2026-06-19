#include "boot.h"

#include "boot_flags.h"
#include "dfu.h"
#include "emmc.h"
#include "memory_map.h"
#include "uart.h"

static uint8_t current_sector[SECTOR_SIZE];
static uint32_t current_sector_counter;
static uint32_t app_ram_offset;

StatusCode boot_init()
{
  current_sector_counter = 0;
  app_ram_offset = 0;

  return E_OK;
}

// Called from bootloader_init()'s cold-boot path (boot/app/main.c) to
// re-derive fw_crc_ok from scratch, since no in-RAM flag can be trusted
// after a reset that may not have preserved RAM contents.
// STUB: always reports valid. Replace with a real CRC/signature check
// against the eMMC-resident app image (EMMC_SECTOR_APP) before relying on
// this in the field.
StatusCode boot_validateApp()
{
  return E_OK;
}

StatusCode boot_loadApp()
{
  uart_print("Loading app");

  if (boot_flags.fw_crc_ok != 1) {
    return E_CORRUPTED;
  }

  // Read the first sector to get our metadata
  emmc_read_blocks(EMMC_SECTOR_APP, current_sector, 1U);

  StartPacket *start_pkt = (StartPacket *)current_sector;

  uart_printf("App details: version: %u length: %uB\r\n", start_pkt->version_num, start_pkt->app_length);

  uint32_t ulSectors = BYTES_TO_SECTORS(start_pkt->app_length);

  emmc_read_blocks(EMMC_SECTOR_APP + 1, (void *)APP_START_ADDR, ulSectors);

  uart_print("App hex dump (4 bytes = 1 ARM instruction):\r\n");
  for (uint32_t i = 0; i < 64; i += 4) {
    uart_printf("  %05x: %02X %02X %02X %02X\r\n",
                APP_START_ADDR + i,
                ((unsigned char *)APP_START_ADDR)[i + 0],
                ((unsigned char *)APP_START_ADDR)[i + 1],
                ((unsigned char *)APP_START_ADDR)[i + 2],
                ((unsigned char *)APP_START_ADDR)[i + 3]);
  }

  // verify CRC

  return E_OK;
}

void boot_jumpToApp()
{
  uart_print( "boot: jumping to app\r\n" );

  // Wait for PL011 TX FIFO to drain before jumping — the app's uart_init()
  // disables the UART immediately and will return E_TIMED_OUT if FR_BUSY is
  // still set, leaving the UART in a broken state.
  while (UART0->FR & FR_BUSY) {}

  // disable interrupts so bootloader IRQs do not fire in the app
  asm volatile ("cpsid if" ::: "memory");

  // memory barrier — ensure all memory writes are visible
  asm volatile ("dsb sy" ::: "memory");
  asm volatile ("isb"    ::: "memory");

  // jump — cast load address to a function pointer and call it
  void (*appEntry)( void ) = (void (*)(void)) APP_START_ADDR;
  appEntry();
}
