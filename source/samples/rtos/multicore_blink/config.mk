# Drivers to compile. reset = enter_bootloader for the DFU trigger.
SAMPLE_DRIVERS         := gpio uart gentimer gic jtag reset interrupts dma watchdog emmc crc

# RTOS kernel components — includes companion_core (secondary-core release) plus the full
# software_timer subsystem (matches every other RTOS sample: full watchdog
# kick/confirm path), to test whether its absence was the differentiator
# between multicore_blink (fails) and the other samples (all pass).
SAMPLE_KERNEL          := scheduler task semaphore heap companion_core spinlock software_timer

# Boot-domain components — dfu_trigger gives the running-app DFU trigger
# (DF 00 DF 00 over UART).
SAMPLE_BOOT_COMPONENTS := dfu_trigger boot_flags

# Utility libraries
SAMPLE_LIBS            :=

# UART_TX_DMA stays disabled (unchanged from before) — only the
# watchdog/software_timer presence is being varied in this test.
SAMPLE_EXTRA_CFLAGS := -DUART_TX_DMA=0
