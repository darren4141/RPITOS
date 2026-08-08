# Drivers to compile
SAMPLE_DRIVERS         := gpio uart emmc crc

# No RTOS kernel, no boot-domain components, no libraries
SAMPLE_KERNEL          :=
SAMPLE_BOOT_COMPONENTS :=
SAMPLE_LIBS            :=

# Uses its own startup.s (source/samples/boot/bootstrap/startup.s), picked up
# automatically by the per-sample startup.s rule in the Makefile.

# Boot-sequence telemetry — links telemetry_frame.o + telemetry_boot.o (the
# unlocked, no-RTOS senders; see md/client/device/boot_init_tracking.md).
# Not the app-side SAMPLE_TELEMETRY=1 path — no scheduler/companion_core here.
SAMPLE_TELEMETRY_BOOT := 1

# Extra compiler flags
SAMPLE_EXTRA_CFLAGS    := -march=armv8-a+crc -DUART_MINIMAL -DRTOS_TELEMETRY
