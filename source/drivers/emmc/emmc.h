#ifndef EMMC_H
#define EMMC_H

#include <stdint.h>

#include "status.h"

#define SECTOR_SIZE                 512
#define BYTES_TO_SECTORS(x) ((x) + SECTOR_SIZE - 1) / SECTOR_SIZE

// eMMC sector layout
//
//  0      – 2047  : VideoCore FAT32 partition (kernel7l.img = bootstrap)
//                   DO NOT TOUCH — GPU reads files from here by name
//  2048   – 2559  : Bootloader slot A  (header @ 2048, binary @ 2049+)
//  2560   – 4095  : Bootloader slot B  (reserved for future A/B update)
//  4096   – 20479 : App                (header @ 4096, binary @ 4097+)
//  20480          : Metadata
//  20481  – 36864 : DFU staging buffer

#define EMMC_SECTOR_VIDEOCORE         0
#define EMMC_SECTOR_BOOTLOADER        2048   // header sector; binary at +1
#define EMMC_SECTOR_BOOTLOADER_B      2560   // reserved — future A/B slot
#define EMMC_SECTOR_APP               4096   // header sector; binary at +1
#define EMMC_SECTOR_METADATA          20480
#define EMMC_SECTOR_DFU_BUFFER        20481

// sizes (in sectors)
#define EMMC_SECTOR_SIZE_BOOTLOADER   512    // 256 KB per bootloader slot
#define EMMC_SECTOR_SIZE_BOOTLOADER_B 512
#define EMMC_SECTOR_SIZE_APP          16384  // 8 MB for app
#define EMMC_SECTOR_SIZE_METADATA     1      // 512 bytes, one sector
#define EMMC_SECTOR_SIZE_DFU_BUFFER   16384  // 8 MB scratch space

// byte addresses (for documentation)
#define EMMC_BYTE_VIDEOCORE           (EMMC_SECTOR_VIDEOCORE  * SECTOR_SIZE)
#define EMMC_BYTE_BOOTLOADER          (EMMC_SECTOR_BOOTLOADER * SECTOR_SIZE)
#define EMMC_BYTE_APP                 (EMMC_SECTOR_APP        * SECTOR_SIZE)
#define EMMC_BYTE_METADATA            (EMMC_SECTOR_METADATA   * SECTOR_SIZE)

#define EMMC2_BASE                  0xFE340000

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
  uint32_t _pad1[7];
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
} EMMC2Regs_t;

static volatile EMMC2Regs_t * const pxEMMC =
  (volatile EMMC2Regs_t *)EMMC2_BASE;

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

#define INT_ERROR_MASK         (INT_CTO_ERR | INT_CCRC_ERR | INT_CEND_ERR | \
                                INT_CBAD_ERR | INT_DTO_ERR | INT_DCRC_ERR | \
                                INT_DEND_ERR | INT_ACMD_ERR | INT_ERR)

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

// eMMC commands

#define CMD0                   (CMD_INDEX(0) | CMD_RESP_NONE)
#define CMD1                   (CMD_INDEX(1) | CMD_RESP_48)
#define CMD2                   (CMD_INDEX(2) | CMD_RESP_136 | CMD_CRCCHK_EN)
#define CMD3                   (CMD_INDEX(3) | CMD_RESP_48 | CMD_CRCCHK_EN | CMD_IXCHK_EN)
#define CMD6_SWITCH            (CMD_INDEX(6) | CMD_RESP_48B | CMD_CRCCHK_EN | CMD_IXCHK_EN)
#define CMD7                   (CMD_INDEX(7) | CMD_RESP_48B | CMD_CRCCHK_EN | CMD_IXCHK_EN)
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

StatusCode emmc_init( void );
StatusCode emmc_read_blocks( uint32_t ulSector, void *pvBuf, uint32_t ulCount );
StatusCode emmc_write_blocks( uint32_t ulSector, const void *pvBuf, uint32_t ulCount );

#endif
