#include "boot.h"

#include "boot_flags.h"
#include "crc.h"
#include "dfu_receive.h"
#include "emmc.h"
#include "memory_map.h"
#include "telemetry.h"
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

StatusCode boot_validate_app(uint32_t app_sector)
{
  emmc_read_blocks(app_sector, current_sector, 1U);
  const StartPacket *start_pkt = (const StartPacket *)current_sector;

  if ((start_pkt->version_num != 1) || (start_pkt->fw_length == 0)) {
#ifdef RTOS_TELEMETRY
    // No binary CRC computed yet for a header this malformed — report what was
    // read, with actual_crc=0/crc_ok=0 so the host still sees the failure.
    telemetry_report_boot_info_image_header(BOOT_STAGE_BOOTLOADER, start_pkt->version_num,
                                             start_pkt->fw_length, start_pkt->crc, 0U, 0U);
#endif
    return E_CORRUPTED;
  }

  uint32_t remaining = start_pkt->fw_length;
  uint32_t expected_crc = start_pkt->crc;
  uint32_t version_num = start_pkt->version_num;
  uint32_t fw_length = start_pkt->fw_length;
  uint32_t sector = app_sector + 1;

  CRC32 ctx;
  crc32_start(&ctx);

  while (remaining > 0) {
    emmc_read_blocks(sector, current_sector, 1U);
    uint32_t chunk = (remaining < SECTOR_SIZE) ? remaining : SECTOR_SIZE;
    crc32_update(&ctx, current_sector, chunk);
    remaining -= chunk;
    sector++;
  }

  uint32_t actual_crc = crc32_finish(&ctx);
  uart_printf("boot_validate_app CRC | Expected: 0x%08X | Actual: 0x%08X\r\n", expected_crc, actual_crc);

#ifdef RTOS_TELEMETRY
  telemetry_report_boot_info_image_header(BOOT_STAGE_BOOTLOADER, version_num, fw_length,
                                           expected_crc, actual_crc, (actual_crc == expected_crc) ? 1U : 0U);
#endif

  return (actual_crc == expected_crc) ? E_OK : E_CORRUPTED;
}

StatusCode boot_load_app(uint32_t app_sector)
{
  uart_print("Loading app");

  // Read the first sector to get our metadata
  emmc_read_blocks(app_sector, current_sector, 1U);

  StartPacket *start_pkt = (StartPacket *)current_sector;

  uart_printf("App details: version: %u length: %uB\r\n", start_pkt->version_num, start_pkt->fw_length);

  uint32_t sectors = BYTES_TO_SECTORS(start_pkt->fw_length);

  // Time the bulk read to see how much time ADMA2 saves
  uint32_t frq, lo, hi;
  asm volatile ("mrc  p15, 0, %0, c14, c0, 0" : "=r" (frq));
  asm volatile ("mrrc p15, 0, %0, %1, c14"   : "=r" (lo), "=r" (hi));
  uint64_t t_start = ((uint64_t)hi << 32) | lo;

  emmc_read_blocks(app_sector + 1, (void *)APP_START_ADDR, sectors);

  asm volatile ("mrrc p15, 0, %0, %1, c14" : "=r" (lo), "=r" (hi));
  uint64_t t_end = ((uint64_t)hi << 32) | lo;

  uint64_t bytes = (uint64_t)sectors * SECTOR_SIZE;
  uint32_t us = (frq != 0) ? (uint32_t)((t_end - t_start) * 1000000ULL / frq) : 0;
  uint32_t kbps = (us != 0) ? (uint32_t)(bytes * 1000000ULL / ((uint64_t)us * 1024)) : 0;
  uart_printf("boot: loaded %u sectors (%uB) in %u us (%u KB/s)\r\n",
              sectors, (uint32_t)bytes, us, kbps);

#ifdef RTOS_TELEMETRY
  telemetry_report_boot_milestone(BOOT_MS_APP_LOAD_DONE, BOOT_STAGE_BOOTLOADER, 0);
#endif

  return E_OK;
}

void boot_jump_to_app()
{
  uart_print("boot: jumping to app\r\n");
#ifdef RTOS_TELEMETRY
  telemetry_report_boot_milestone(BOOT_MS_JUMPING_TO_APP, BOOT_STAGE_BOOTLOADER, 0);
#endif

  // Wait for UART PL011 TX FIFO to drain before jumping
  while (UART0->FR & FR_BUSY) {}

  // disable interrupts so bootloader IRQs do not fire in the app
  asm volatile ("cpsid if" ::: "memory");

  // memory barrier - ensure all memory writes are visible
  asm volatile ("dsb sy" ::: "memory");
  asm volatile ("isb"    ::: "memory");

  // jump
  void (*app_entry)(void) = (void (*)(void)) APP_START_ADDR;
  app_entry();
}
