#ifndef MEMORY_MAP_H
#define MEMORY_MAP_H

#define BOOTSTRAP_START_ADDR  0x8000    // GPU loads bootstrap here (kernel7l.img)
#define BOOTLOADER_START_ADDR 0x10000   // bootstrap loads bootloader here
#define BOOT_FLAGS_START_ADDR 0x88000   // shared RAM between bootloader and app
#define APP_START_ADDR        0x88400   // bootloader loads app here

#endif
