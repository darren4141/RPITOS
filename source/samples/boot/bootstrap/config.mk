# Drivers to compile
SAMPLE_DRIVERS         := gpio uart emmc crc

# No RTOS kernel, no boot-domain components, no libraries
SAMPLE_KERNEL          :=
SAMPLE_BOOT_COMPONENTS :=
SAMPLE_LIBS            :=

# Uses its own startup.s (source/samples/boot/bootstrap/startup.s), picked up
# automatically by the per-sample startup.s rule in the Makefile.

# Extra compiler flags
SAMPLE_EXTRA_CFLAGS    := -march=armv8-a+crc -DUART_MINIMAL
