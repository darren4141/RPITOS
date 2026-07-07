# Drivers to compile
SAMPLE_DRIVERS         := gpio uart gentimer gic jtag reset interrupts watchdog emmc crc dma

# RTOS kernel components to compile
SAMPLE_KERNEL          := scheduler task semaphore mutex queue heap

# Boot-domain components
SAMPLE_BOOT_COMPONENTS := dfu_trigger boot_flags

# Utility libraries
SAMPLE_LIBS            := delay

# Extra compiler flags specific to this sample
SAMPLE_EXTRA_CFLAGS    :=