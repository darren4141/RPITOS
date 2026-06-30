SAMPLE_DRIVERS         := gpio uart emmc crc

SAMPLE_KERNEL          :=
SAMPLE_BOOT_COMPONENTS :=
SAMPLE_LIBS            :=

SAMPLE_STARTUP_OVERRIDE := source/samples/boot/bootloader/startup.s

SAMPLE_EXTRA_CFLAGS    := -march=armv8-a+crc -DUART_MINIMAL
