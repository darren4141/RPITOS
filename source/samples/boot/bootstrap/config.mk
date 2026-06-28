# Drivers to compile
SAMPLE_DRIVERS         := gpio uart emmc crc

# No RTOS kernel, no boot-domain components, no libraries
SAMPLE_KERNEL          :=
SAMPLE_BOOT_COMPONENTS :=
SAMPLE_LIBS            :=

# Reuse the bootloader's startup.s (HYP exit, banked stacks, BSS zero, VBAR)
SAMPLE_STARTUP_OVERRIDE := source/samples/boot/bootloader/startup.s

# Extra compiler flags
SAMPLE_EXTRA_CFLAGS    := -march=armv8-a+crc -DUART_MINIMAL
