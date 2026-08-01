# Drivers to compile. reset = enter_bootloader for the DFU trigger.
SAMPLE_DRIVERS         := gpio uart gentimer gic jtag reset interrupts dma watchdog emmc crc

# RTOS kernel components — full set, including companion_core (per-core
# scheduler bring-up) and the synchronization primitives this sample exists
# to demonstrate cross-core: mutex, semaphore, queue.
SAMPLE_KERNEL          := scheduler task semaphore mutex queue heap companion_core spinlock software_timer

# Boot-domain components — dfu_trigger gives the running-app DFU trigger
# (DF 00 DF 00 over UART).
SAMPLE_BOOT_COMPONENTS := dfu_trigger boot_flags

# Utility libraries
SAMPLE_LIBS            :=
