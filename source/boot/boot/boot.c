#include "boot.h"

#include "boot_flags.h"
#include "crc.h"
#include "dfu_receive.h"
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

StatusCode boot_validateApp()
{
  emmc_read_blocks(EMMC_SECTOR_APP, current_sector, 1U);
  const StartPacket *start_pkt = (const StartPacket *)current_sector;

  if ((start_pkt->version_num != 1) || (start_pkt->fw_length == 0)) {
    return E_CORRUPTED;
  }

  uint32_t remaining = start_pkt->fw_length;
  uint32_t expected_crc = start_pkt->crc;
  uint32_t sector = EMMC_SECTOR_APP + 1;

  CRC32_t ctx;
  crc32_start(&ctx);

  while (remaining > 0) {
    emmc_read_blocks(sector, current_sector, 1U);
    uint32_t chunk = (remaining < SECTOR_SIZE) ? remaining : SECTOR_SIZE;
    crc32_update(&ctx, current_sector, chunk);
    remaining -= chunk;
    sector++;
  }

  uint32_t actual_crc = crc32_finish(&ctx);
  uart_printf("boot_validateApp CRC | Expected: 0x%08X | Actual: 0x%08X\r\n", expected_crc, actual_crc);

  return (actual_crc == expected_crc) ? E_OK : E_CORRUPTED;
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

  uart_printf("App details: version: %u length: %uB\r\n", start_pkt->version_num, start_pkt->fw_length);

  uint32_t ulSectors = BYTES_TO_SECTORS(start_pkt->fw_length);

  emmc_read_blocks(EMMC_SECTOR_APP + 1, (void *)APP_START_ADDR, ulSectors);

  uart_print("App hex dump (4 bytes = 1 ARM instruction):\r\n");
  for (uint32_t i = 0; i < 4; i += 4) {
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
