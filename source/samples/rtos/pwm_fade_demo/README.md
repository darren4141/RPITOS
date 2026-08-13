# pwm_fade_demo

PCA9685 PWM crossfade demo for the Raspberry Pi CM4.

Drives the same two PCA9685 outputs `multicore_full_demo`'s `pca_blink_task`
full-on/full-off blinks (channels 4 and 5, chip on `I2C_CHANNEL_3` /
GPIO4-5 `ALT5`) — but here with actual PWM duty-cycle modulation instead of
a hard on/off snap. A single task ramps channel 4 from 0% to 100% duty over
100 steps while channel 5 ramps the complement (100% to 0%), then reverses,
producing a continuous 4-second breathing crossfade between the two LEDs.

`pwm_pca9685_set_channel(channel, 0, duty_cycle)` is called once per step
with `duty_cycle` a fraction of `UINT32_MAX`, scaled internally by the driver
to the chip's 12-bit tick resolution (0-4095) — see
[pwm_pca9685/docs.md](../../../drivers/pwm_pca9685/docs.md).

Single core — no cross-core synchronization primitives, unlike
`multicore_full_demo`. Only the uart, dfu-trigger, and watchdog tasks are
pulled in besides the fade task itself.

## Build

```bash
make SAMPLE=rtos/pwm_fade_demo
```

Output: `build/rtos/pwm_fade_demo/pwm_fade_demo.elf / .img / .hex`

## Components used

- **Drivers**: `gpio`, `uart`, `gentimer`, `gic`, `jtag`, `reset`, `interrupts`, `dma`, `watchdog`, `emmc`, `crc`, `i2c`, `mailbox`, `pwm_pca9685`
- **Kernel**: `scheduler`, `task`, `semaphore`, `heap`, `software_timer`
- **Boot library**: `dfu_trigger`, `boot_flags`
