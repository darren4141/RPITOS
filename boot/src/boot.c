#include "boot.h"

#include "boot_flags.h"
#include "dfu.h"
#include "emmc.h"
#include "memory_map.h"

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
  return E_OK;
}

StatusCode boot_loadApp()
{
  if (boot_flags.fw_crc_ok != 1) {
    return E_CORRUPTED;
  }

  // Read the first sector to get our metadata
  emmc_read_blocks(EMMC_SECTOR_APP, current_sector, 1U);

  StartPacket *start_pkt = (StartPacket *)current_sector;

  uint32_t ulSectors = BYTES_TO_SECTORS(start_pkt->app_length);

  emmc_read_blocks(EMMC_SECTOR_APP + 1, (void *)APP_START_ADDR, ulSectors);

  // verify CRC

  return E_OK;
}

void boot_jumpToApp()
{
  uart_print( "boot: jumping to app\r\n" );

  // We might have to flush UART

  // disable interrupts so bootloader IRQs do not fire in the app
  asm volatile ("cpsid if" ::: "memory");

  // memory barrier — ensure all memory writes are visible
  asm volatile ("dsb sy" ::: "memory");
  asm volatile ("isb"    ::: "memory");

  // jump — cast load address to a function pointer and call it
  void (*appEntry)( void ) = (void (*)(void)) APP_START_ADDR;
  appEntry();
}
