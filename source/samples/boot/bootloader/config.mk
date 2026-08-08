# Drivers to compile
SAMPLE_DRIVERS         := gpio uart emmc crc jtag watchdog

# RTOS kernel components (none — bootloader does not use the RTOS)
SAMPLE_KERNEL          :=

# Boot-domain components
SAMPLE_BOOT_COMPONENTS := boot dfu_receive dfu_trigger boot_flags

# Utility libraries (none — bootloader has no tick source for delay)
SAMPLE_LIBS            :=

# Boot-sequence + DFU telemetry — links telemetry_frame.o + telemetry_boot.o
# (the unlocked, no-RTOS senders; see md/client/device/boot_init_tracking.md).
# Not the app-side SAMPLE_TELEMETRY=1 path — no scheduler/companion_core here.
SAMPLE_TELEMETRY_BOOT := 1

# Extra compiler flags specific to this sample
SAMPLE_EXTRA_CFLAGS    := -march=armv8-a+crc -DUART_MINIMAL -DWATCHDOG_MINIMAL -DRTOS_TELEMETRY
