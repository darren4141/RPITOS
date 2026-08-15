# Drivers to compile. Same baseline as full_demo (watchdog/A-B boot infra)
# plus cprman (PCM clock generator) + i2s (the peripheral itself). No i2c/
# mailbox — unlike pwm_fade_demo, nothing here needs them.
SAMPLE_DRIVERS         := gpio uart gentimer gic jtag reset interrupts watchdog emmc crc dma cprman i2s

# RTOS kernel components. companion_core/spinlock are pulled in to match
# every other RTOS sample's watchdog kick/confirm path (scheduler.c/
# semaphore.c/heap.c all reference them even single-core) — no mutex/queue
# needed since nothing here uses them.
SAMPLE_KERNEL          := scheduler task semaphore heap companion_core spinlock software_timer

# Boot-domain components — dfu_trigger gives the running-app DFU trigger
# (DF 00 DF 00 over UART); boot_flags backs the A/B trial-boot confirm.
SAMPLE_BOOT_COMPONENTS := dfu_trigger boot_flags

# Utility libraries
SAMPLE_LIBS            :=

SAMPLE_EXTRA_CFLAGS    :=
