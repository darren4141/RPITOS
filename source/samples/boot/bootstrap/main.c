#include <stdint.h>

#include "crc.h"
#include "dfu_receive.h"
#include "emmc.h"
#include "uart.h"

// RAM address the bootloader binary is loaded to before jumping.
// Must match BOOTLOADER_START_ADDR in memory_map.h (not included here to keep
// the bootstrap free of boot-domain dependencies).
#define BOOTLOADER_LOAD_ADDR 0x10000U

void kmain(void)
{
  uart_init(UART_BAUDRATE_115200);
  uart_print("bootstrap: starting\r\n");

  StatusCode ret = emmc_init();
  if (ret != E_OK) {
    uart_printf("bootstrap: emmc init failed (%d), hanging\r\n", ret);
    while (1) {}
  }

  uart_print("emmc successfully initialized\r\n");

  // Read bootloader header from eMMC sector EMMC_SECTOR_BOOTLOADER.
  // aligned(4) ensures the StartPacket* cast below sees word-aligned uint32_t fields.
  uint8_t header_buf[SECTOR_SIZE] __attribute__((aligned(4)));
  ret = emmc_read_blocks(EMMC_SECTOR_BOOTLOADER, header_buf, 1U);
  if (ret != E_OK) {
    uart_printf("bootstrap: header read failed (%d), hanging\r\n", ret);
    while (1) {}
  }

  const StartPacket *hdr = (const StartPacket *)header_buf;
  uart_printf("bootstrap: bootloader v%u, %u bytes\r\n",
              hdr->version_num, hdr->fw_length);

  if (hdr->fw_length == 0) {
    uart_print("bootstrap: invalid header (fw_length=0), hanging\r\n");
    while (1) {}
  }

  // Load bootloader binary into RAM starting at BOOTLOADER_LOAD_ADDR.
  uint32_t sectors = BYTES_TO_SECTORS(hdr->fw_length);
  ret = emmc_read_blocks(EMMC_SECTOR_BOOTLOADER + 1U,
                         (void *)BOOTLOADER_LOAD_ADDR, sectors);
  if (ret != E_OK) {
    uart_printf("bootstrap: binary read failed (%d), hanging\r\n", ret);
    while (1) {}
  }

  // Validate CRC32 of the loaded binary.
  CRC32_t ctx;
  crc32_start(&ctx);
  crc32_update(&ctx, (const void *)BOOTLOADER_LOAD_ADDR, hdr->fw_length);
  uint32_t actual_crc = crc32_finish(&ctx);

  if (actual_crc != hdr->crc) {
    uart_printf("bootstrap: CRC mismatch — expected 0x%08X got 0x%08X, hanging\r\n",
                hdr->crc, actual_crc);
    while (1) {}
  }

  uart_print("bootstrap: CRC OK, jumping to bootloader\r\n");
  uart_drain();

  __asm__ volatile ("dsb sy" ::: "memory");
  __asm__ volatile ("isb"    ::: "memory");

  void (*bootEntry)(void) = (void (*)(void)) BOOTLOADER_LOAD_ADDR;
  bootEntry();
}
