#include <stdint.h>

#include "crc.h"
#include "dfu_receive.h"
#include "emmc.h"
#include "uart.h"

#define BOOTLOADER_LOAD_ADDR 0x10000U

void kmain(void)
{
  uart_init(UART_BAUDRATE_115200);
  uart_print("wiper: starting\r\n");

  StatusCode ret = emmc_init();
  if (ret != E_OK) {
    uart_printf("wiper: emmc init failed (%d), hanging\r\n", ret);
    while (1) {}
  }

  uart_print("wiper: emmc initialized\r\n");

  // Zero both app slot headers and the metadata sector so the bootloader sees
  // no valid app in either A/B slot and (bad metadata magic) defaults to slot A.
  static uint8_t zero_block[SECTOR_SIZE];
  const uint32_t wipe_sectors[] = {
    EMMC_SECTOR_APP_A, EMMC_SECTOR_APP_B, EMMC_SECTOR_METADATA
  };
  for (uint32_t i = 0; i < sizeof(wipe_sectors) / sizeof(wipe_sectors[0]); i++) {
    ret = emmc_write_blocks(wipe_sectors[i], zero_block, 1U);
    if (ret != E_OK) {
      uart_printf("wiper: sector %u write failed (%d), hanging\r\n", wipe_sectors[i], ret);
      while (1) {}
    }
  }
  uart_print("wiper: app slot A/B headers + metadata zeroed\r\n");

  // Clear boot_flags magic so the bootloader treats this as a cold boot
  // and re-validates the (now-zeroed) app header.
  uint32_t *bflags = (uint32_t *)0x88000U;
  for (uint32_t i = 0; i < 64U; i++) bflags[i] = 0U;
  uart_print("wiper: boot_flags cleared\r\n");

  // Load bootloader from eMMC sector 2048 (same as normal bootstrap).
  uint8_t header_buf[SECTOR_SIZE] __attribute__((aligned(4)));
  ret = emmc_read_blocks(EMMC_SECTOR_BOOTLOADER, header_buf, 1U);
  if (ret != E_OK) {
    uart_printf("wiper: bootloader header read failed (%d), hanging\r\n", ret);
    while (1) {}
  }

  const StartPacket *hdr = (const StartPacket *)header_buf;
  uart_printf("wiper: bootloader v%u, %u bytes\r\n", hdr->version_num, hdr->fw_length);

  if (hdr->fw_length == 0) {
    uart_print("wiper: invalid bootloader header, hanging\r\n");
    while (1) {}
  }

  uint32_t sectors = BYTES_TO_SECTORS(hdr->fw_length);
  ret = emmc_read_blocks(EMMC_SECTOR_BOOTLOADER + 1U, (void *)BOOTLOADER_LOAD_ADDR, sectors);
  if (ret != E_OK) {
    uart_printf("wiper: bootloader binary read failed (%d), hanging\r\n", ret);
    while (1) {}
  }

  CRC32_t ctx;
  crc32_start(&ctx);
  crc32_update(&ctx, (const void *)BOOTLOADER_LOAD_ADDR, hdr->fw_length);
  uint32_t actual_crc = crc32_finish(&ctx);

  if (actual_crc != hdr->crc) {
    uart_printf("wiper: bootloader CRC mismatch — expected 0x%08X got 0x%08X, hanging\r\n",
                hdr->crc, actual_crc);
    while (1) {}
  }

  uart_print("wiper: CRC OK, jumping to bootloader — expect DFU mode\r\n");
  uart_drain();

  __asm__ volatile ("dsb sy" ::: "memory");
  __asm__ volatile ("isb"    ::: "memory");

  void (*bootEntry)(void) = (void (*)(void)) BOOTLOADER_LOAD_ADDR;
  bootEntry();
}
