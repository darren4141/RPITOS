# Drivers to compile. reset = enter_bootloader for the DFU trigger. mailbox is
# i2c's dependency (BSC core clock query); pwm_pca9685 is the PCA9685 PWM
# driver built on top of i2c.
SAMPLE_DRIVERS         := gpio uart gentimer gic jtag reset interrupts dma watchdog emmc crc i2c mailbox pwm_pca9685

# RTOS kernel components — companion_core/software_timer pulled in to match
# every other RTOS sample's watchdog kick/confirm path, even though this
# sample itself is single-core.
SAMPLE_KERNEL          := scheduler task semaphore heap companion_core spinlock software_timer

# Boot-domain components — dfu_trigger gives the running-app DFU trigger
# (DF 00 DF 00 over UART).
SAMPLE_BOOT_COMPONENTS := dfu_trigger boot_flags

# Utility libraries
SAMPLE_LIBS            :=

SAMPLE_EXTRA_CFLAGS    :=
