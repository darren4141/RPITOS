#ifndef EMMC_H
#define EMMC_H

// eMMC sector layout
#define EMMC_SECTOR_VIDEOCORE   0            // sector 0-2047: VideoCore files
                                             // DO NOT TOUCH — GPU needs these
#define EMMC_SECTOR_BOOTLOADER  2048         // sector 2048+: your kernel8.img
#define EMMC_SECTOR_APP         4096         // sector 4096+: app binary
#define EMMC_SECTOR_METADATA    8192         // sector 8192+: firmware metadata
#define EMMC_SECTOR_DFU_BUFFER  8256         // sector 8256+: scratch space

// sizes (in sectors)
#define EMMC_SECTORS_BOOTLOADER 512          // 256KB for bootloader
#define EMMC_SECTORS_APP        16384        // 8MB for app
#define EMMC_SECTORS_METADATA   1            // 512 bytes, one sector
#define EMMC_SECTORS_DFU_BUFFER 16384        // 8MB scratch space

// byte addresses (for documentation)
#define EMMC_BYTE_VIDEOCORE     (EMMC_SECTOR_VIDEOCORE * SECTOR_SIZE)
#define EMMC_BYTE_APP           (EMMC_SECTOR_APP * SECTOR_SIZE)
#define EMMC_BYTE_METADATA      (EMMC_SECTOR_METADATA * SECTOR_SIZE)

#endif
