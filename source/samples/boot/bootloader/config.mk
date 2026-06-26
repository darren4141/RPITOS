# Drivers to compile
SAMPLE_DRIVERS         := gpio uart emmc crc jtag

# RTOS kernel components (none — bootloader does not use the RTOS)
SAMPLE_KERNEL          :=

# Boot-domain components
SAMPLE_BOOT_COMPONENTS := boot dfu_receive dfu_trigger boot_flags

# Utility libraries (none — bootloader has no tick source for delay)
SAMPLE_LIBS            :=

# Extra compiler flags specific to this sample
SAMPLE_EXTRA_CFLAGS    := -march=armv8-a+crc -DUART_MINIMAL
