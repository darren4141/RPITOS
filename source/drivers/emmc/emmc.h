#ifndef EMMC_H
#define EMMC_H

#include <stdint.h>

#include "status.h"

#define SECTOR_SIZE                   512
#define BYTES_TO_SECTORS(x) ((x) + SECTOR_SIZE - 1) / SECTOR_SIZE

// eMMC sector layout — see docs.md for the full partition/slot map.
#define EMMC_FW_BASE                  1064960U                // partition-2 start LBA

#define EMMC_SECTOR_BOOTLOADER        (EMMC_FW_BASE + 0U)     // header; binary at +1
#define EMMC_SECTOR_BOOTLOADER_B      (EMMC_FW_BASE + 512U)   // reserved — future A/B
#define EMMC_SECTOR_METADATA          (EMMC_FW_BASE + 1024U)
#define EMMC_SECTOR_APP_A             (EMMC_FW_BASE + 2048U)  // header; binary at +1
#define EMMC_SECTOR_APP_B             (EMMC_FW_BASE + 18432U) // header; binary at +1
#define EMMC_SECTOR_APP               EMMC_SECTOR_APP_A       // default active-slot alias

// Firmware must NEVER write below this LBA — this is the choke point that
// protects the MBR, the alignment gap, and the entire FAT boot partition.
// Enforced in emmc_write_blocks().
#define EMMC_FIRMWARE_FLOOR           EMMC_SECTOR_BOOTLOADER

// sizes (in sectors)
#define EMMC_SECTOR_SIZE_BOOTLOADER   512    // 256 KB per bootloader slot
#define EMMC_SECTOR_SIZE_BOOTLOADER_B 512
#define EMMC_SECTOR_SIZE_APP          16384  // 8 MB per app slot
#define EMMC_SECTOR_SIZE_METADATA     1      // 512 bytes, one sector

// App slot identifiers, stored in metadata (WdtMeta.active_app_slot). Distinct
// 32-bit patterns so a torn/half-erased field is unlikely to alias a valid slot.
#define APP_SLOT_A                    0xAAAAAAAAU
#define APP_SLOT_B                    0xBBBBBBBBU

// Resolve a slot id to its header sector; any unknown value defaults to slot A.
#define APP_SLOT_TO_SECTOR(slot) ((slot) == APP_SLOT_B ? EMMC_SECTOR_APP_B : EMMC_SECTOR_APP_A)
// The opposite (inactive) slot — the one DFU writes into.
#define APP_SLOT_OTHER(slot)     ((slot) == APP_SLOT_B ? APP_SLOT_A : APP_SLOT_B)
// Human-readable slot letter for logging.
#define APP_SLOT_LETTER(slot)    ((slot) == APP_SLOT_B ? "B" : "A")

// byte addresses (for documentation)
#define EMMC_BYTE_BOOTLOADER          (EMMC_SECTOR_BOOTLOADER * SECTOR_SIZE)
#define EMMC_BYTE_APP                 (EMMC_SECTOR_APP * SECTOR_SIZE)
#define EMMC_BYTE_METADATA            (EMMC_SECTOR_METADATA * SECTOR_SIZE)

#define EMMC2_BASE                    0xFE340000

typedef struct {
  uint32_t ARG2;            // 0x00  ACMD23 argument
  uint32_t BLKSIZECNT;      // 0x04  block size and count
  uint32_t ARG1;            // 0x08  command argument
  uint32_t CMDTM;           // 0x0C  command and transfer mode
  uint32_t RESP0;           // 0x10  response word 0
  uint32_t RESP1;           // 0x14  response word 1
  uint32_t RESP2;           // 0x18  response word 2
  uint32_t RESP3;           // 0x1C  response word 3
  uint32_t DATA;            // 0x20  data
  uint32_t STATUS;          // 0x24  present state
  uint32_t CONTROL0;        // 0x28  host control 0
  uint32_t CONTROL1;        // 0x2C  host control 1
  uint32_t INTERRUPT;       // 0x30  interrupt flags
  uint32_t IRPT_MASK;       // 0x34  interrupt flag enable
  uint32_t IRPT_EN;         // 0x38  interrupt generation enable
  uint32_t CONTROL2;        // 0x3C  host control 2
  uint32_t CAPABILITIES0;   // 0x40
  uint32_t CAPABILITIES1;   // 0x44
  uint32_t _pad0[2];
  uint32_t FORCE_IRPT;      // 0x50
  uint32_t ADMA_ERROR;      // 0x54  ADMA error status
  uint32_t ADMA_ADDRESS;    // 0x58  ADMA system-address (32-bit descriptor table)
  uint32_t _pad1[5];        // 0x5C..0x6F
  uint32_t BOOT_TIMEOUT;    // 0x70
  uint32_t DBG_SEL;         // 0x74
  uint32_t _pad2[2];
  uint32_t EXRDFIFO_CFG;    // 0x80
  uint32_t EXRDFIFO_EN;     // 0x84
  uint32_t TUNE_STEP;       // 0x88
  uint32_t TUNE_STEPS_STD;  // 0x8C
  uint32_t TUNE_STEPS_DDR;  // 0x90
  uint32_t _pad3[23];
  uint32_t SPI_INT_SPT;     // 0xF0
  uint32_t _pad4[2];
  uint32_t SLOTISR_VER;     // 0xFC
} EMMC2Regs;

static volatile EMMC2Regs * const emmc_regs =
  (volatile EMMC2Regs *)EMMC2_BASE;

// CONTROL1
#define CTRL1_CLK_INTLEN       (1 << 0)    // internal clock enable
#define CTRL1_CLK_STABLE       (1 << 1)    // internal clock stable
#define CTRL1_CLK_EN           (1 << 2)    // SD clock enable
#define CTRL1_CLK_FREQ_MS2     (1 << 6)    // clock frequency MS2 bit
#define CTRL1_CLK_FREQ8        (0xFF << 8)
#define CTRL1_DATA_TOUNIT      (0xF << 16) // data timeout unit
#define CTRL1_SRST_HC          (1 << 24)   // host circuit reset
#define CTRL1_SRST_CMD         (1 << 25)   // command circuit reset
#define CTRL1_SRST_DATA        (1 << 26)   // data circuit reset

// STATUS
#define STATUS_CMD_INHIBIT     (1 << 0)
#define STATUS_DAT_INHIBIT     (1 << 1)
#define STATUS_DAT_ACTIVE      (1 << 2)
#define STATUS_WRITE_TRANS     (1 << 8)
#define STATUS_READ_TRANS      (1 << 9)
#define STATUS_BUFFER_WRITE    (1 << 10)
#define STATUS_BUFFER_READ     (1 << 11)
#define STATUS_CARD_INSERT     (1 << 16)

// INTERRUPT flags
#define INT_CMD_DONE           (1 << 0)
#define INT_DATA_DONE          (1 << 1)
#define INT_BLOCK_GAP          (1 << 2)
#define INT_WRITE_RDY          (1 << 4)
#define INT_READ_RDY           (1 << 5)
#define INT_CARD               (1 << 8)
#define INT_RETUNE             (1 << 12)
#define INT_BOOTACK            (1 << 13)
#define INT_ENDBOOT            (1 << 14)
#define INT_ERR                (1 << 15)
#define INT_CTO_ERR            (1 << 16) // command timeout
#define INT_CCRC_ERR           (1 << 17) // command CRC
#define INT_CEND_ERR           (1 << 18) // command end bit
#define INT_CBAD_ERR           (1 << 19) // command index
#define INT_DTO_ERR            (1 << 20) // data timeout
#define INT_DCRC_ERR           (1 << 21) // data CRC
#define INT_DEND_ERR           (1 << 22) // data end bit
#define INT_ACMD_ERR           (1 << 24) // auto CMD error
#define INT_ADMA_ERR           (1 << 25) // ADMA error (see ADMA_ERROR register)

#define INT_ERROR_MASK         (INT_CTO_ERR | INT_CCRC_ERR | INT_CEND_ERR | \
                                INT_CBAD_ERR | INT_DTO_ERR | INT_DCRC_ERR | \
                                INT_DEND_ERR | INT_ACMD_ERR | INT_ADMA_ERR | INT_ERR)

// CMDTM fields
#define CMD_INDEX(x)             ((x) << 24)
#define CMD_TYPE_NORMAL        (0 << 22)
#define CMD_TYPE_SUSPEND       (1 << 22)
#define CMD_TYPE_RESUME        (2 << 22)
#define CMD_TYPE_ABORT         (3 << 22)
#define CMD_ISDATA             (1 << 21)
#define CMD_IXCHK_EN           (1 << 20)
#define CMD_CRCCHK_EN          (1 << 19)
#define CMD_RESP_NONE          (0 << 16)
#define CMD_RESP_136           (1 << 16)
#define CMD_RESP_48            (2 << 16)
#define CMD_RESP_48B           (3 << 16) // busy after response
#define TM_MULTI_BLOCK         (1 << 5)
#define TM_DAT_DIR_RD          (1 << 4)
#define TM_AUTO_CMD12          (1 << 2)
#define TM_BLKCNT_EN           (1 << 1)
#define TM_DMA_EN              (1 << 0)   // transfer via SDMA/ADMA instead of PIO

// CONTROL0 (Host Control 1) bus width / speed bits
#define CTRL0_4BIT             (1u << 1)  // 4-bit data width
#define CTRL0_HS_EN            (1u << 2)  // High Speed enable
#define CTRL0_8BIT             (1u << 5)  // 8-bit data width (overrides 4-bit)

// CONTROL0 (Host Control 1) DMA-select field, bits [4:3]
#define CTRL0_DMA_SELECT_MASK  (3u << 3)
#define CTRL0_DMA_SELECT_ADMA2 (2u << 3)  // 10b = 32-bit ADMA2

// CAPABILITIES0 — ADMA2 supported (SDHCI capabilities bit 19)
#define CAP0_ADMA2_SUPPORT     (1u << 19)

// ADMA2 descriptor attribute field (low 16 bits of the first 32-bit word):
//   word0 = (length << 16) | attr,  word1 = 32-bit buffer address
#define ADMA2_DESC_VALID       (1u << 0)  // line is valid (else -> ADMA error)
#define ADMA2_DESC_END         (1u << 1)  // last line; stop after it
#define ADMA2_DESC_INT         (1u << 2)  // raise ADMA interrupt when line done
#define ADMA2_DESC_ACT_NOP     (0u << 4)  // skip to next line
#define ADMA2_DESC_ACT_TRAN    (2u << 4)  // transfer len bytes at address
#define ADMA2_DESC_ACT_LINK    (3u << 4)  // address points to another table

// eMMC commands

#define CMD0                   (CMD_INDEX(0) | CMD_RESP_NONE)
#define CMD1                   (CMD_INDEX(1) | CMD_RESP_48)
#define CMD2                   (CMD_INDEX(2) | CMD_RESP_136 | CMD_CRCCHK_EN)
#define CMD3                   (CMD_INDEX(3) | CMD_RESP_48 | CMD_CRCCHK_EN | CMD_IXCHK_EN)
#define CMD6_SWITCH            (CMD_INDEX(6) | CMD_RESP_48B | CMD_CRCCHK_EN | CMD_IXCHK_EN)
#define CMD7                   (CMD_INDEX(7) | CMD_RESP_48B | CMD_CRCCHK_EN | CMD_IXCHK_EN)
#define CMD8                   (CMD_INDEX(8) | CMD_RESP_48 | CMD_CRCCHK_EN | CMD_IXCHK_EN \
                                | CMD_ISDATA | TM_DAT_DIR_RD)   // eMMC SEND_EXT_CSD (512 B read)
#define CMD17                  (CMD_INDEX(17) | CMD_RESP_48 | CMD_CRCCHK_EN | CMD_IXCHK_EN \
                                | CMD_ISDATA | TM_DAT_DIR_RD)
#define CMD18                  (CMD_INDEX(18) | CMD_RESP_48 | CMD_CRCCHK_EN | CMD_IXCHK_EN \
                                | CMD_ISDATA | TM_DAT_DIR_RD | TM_MULTI_BLOCK \
                                | TM_BLKCNT_EN | TM_AUTO_CMD12)
#define CMD24                  (CMD_INDEX(24) | CMD_RESP_48 | CMD_CRCCHK_EN | CMD_IXCHK_EN \
                                | CMD_ISDATA)
#define CMD25                  (CMD_INDEX(25) | CMD_RESP_48 | CMD_CRCCHK_EN | CMD_IXCHK_EN \
                                | CMD_ISDATA | TM_MULTI_BLOCK | TM_BLKCNT_EN | TM_AUTO_CMD12)

// CMD6 SWITCH arg: access=write byte (3), EXT_CSD index, value
#define SWITCH_ARG(index, value) ((3U << 24) | ((uint32_t)(index) << 16) | ((uint32_t)(value) << 8))
#define EXT_CSD_BUS_WIDTH      183
#define EXT_CSD_BUS_WIDTH_1BIT 0
#define EXT_CSD_BUS_WIDTH_4BIT 1
#define EXT_CSD_BUS_WIDTH_8BIT 2

// EXT_CSD byte offsets (read via CMD8 SEND_EXT_CSD)
#define EXT_CSD_HS_TIMING      185  // current speed mode (writable)
#define EXT_CSD_HS_TIMING_HS   1    // HS_TIMING value for High Speed (<=52 MHz)
#define EXT_CSD_REV            192  // EXT_CSD structure revision
#define EXT_CSD_DEVICE_TYPE    196  // CARD_TYPE — supported speed-mode bitmask
#define EXT_CSD_SEC_COUNT      212  // capacity in 512 B sectors (u32 LE)

// EXT_CSD[196] DEVICE_TYPE / CARD_TYPE bits — which speed modes the device supports
#define DEVICE_TYPE_HS26       (1u << 0)  // High Speed 26 MHz
#define DEVICE_TYPE_HS52       (1u << 1)  // High Speed 52 MHz
#define DEVICE_TYPE_DDR52_18V  (1u << 2)  // HS DDR 52 MHz 1.8/3V
#define DEVICE_TYPE_DDR52_12V  (1u << 3)  // HS DDR 52 MHz 1.2V
#define DEVICE_TYPE_HS200_18V  (1u << 4)  // HS200 200 MHz 1.8V
#define DEVICE_TYPE_HS200_12V  (1u << 5)  // HS200 200 MHz 1.2V
#define DEVICE_TYPE_HS400_18V  (1u << 6)  // HS400 200 MHz DDR 1.8V
#define DEVICE_TYPE_HS400_12V  (1u << 7)  // HS400 200 MHz DDR 1.2V

/**
 * @brief Bring up the eMMC controller: reset, clock to 400 kHz, CMD0-CMD7 init sequence, then negotiate bus width/speed.
 * @note Idempotent — returns immediately if a previous boot stage already left the clock enabled and stable.
 */
StatusCode emmc_init( void );

/**
 * @brief Read count sectors starting at sector into buf, via ADMA2 when available, falling back to PIO.
 */
StatusCode emmc_read_blocks( uint32_t sector, void *buf, uint32_t count );

/**
 * @brief Write count sectors starting at sector from buf, via ADMA2 when available, falling back to PIO.
 * @note Refuses to write below EMMC_FIRMWARE_FLOOR.
 */
StatusCode emmc_write_blocks( uint32_t sector, const void *buf, uint32_t count );

#endif
