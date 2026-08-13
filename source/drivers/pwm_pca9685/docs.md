# pwm_pca9685

Driver for the NXP/TI PCA9685, a 16-channel 12-bit PWM I2C LED/servo
controller, built on top of this project's [`i2c`](../i2c/docs.md) driver.
Register map, bit layout, and the `PRE_SCALE` formula are public PCA9685
datasheet facts, not project-specific hardware — no "unverified against real
silicon" caveats apply here the way they do for this project's own BCM2711
peripherals.

Ported from an external single-board driver that assumed a different
platform (libc `printf`/`usleep`, a fixed global I2C bus, `float`-based duty
cycle). See "Changes from the original" below for what moved and why.

## Ownership and prerequisites

`Pca9685Config` is caller-owned, static/global storage duration — same rule
as `UartConfig`/`I2cConfig`. The I2C channel it names must already be
initialized via `i2c_channel_init()` before `pwm_pca9685_init()` is called;
this driver doesn't own or check that itself — `i2c_channel_write()`/
`i2c_channel_read()` already return `E_NOT_INITIALIZED` if it wasn't, so
there's nothing to duplicate here.

`pca9685_read_reg()` (used once, for `MODE1` during init) does a plain
`i2c_channel_write()` then `i2c_channel_read()` — two independent
transactions with a STOP in between — not `i2c_channel_write_read()`'s
repeated-start technique. This isn't a style choice: the repeated-start path
hung on the very first real call on real hardware (`TA` stuck asserted,
`DONE`/`ERR`/`CLKT` never set — see `i2c/docs.md`'s "Repeated start"
section), and the PCA9685 doesn't actually need a true repeated start for a
register read — Adafruit's widely-used PCA9685 library reads registers the
same STOP-then-START way (`Wire.endTransmission()` defaults to sending a
STOP, then `requestFrom()` is a fresh START). If a future change here needs
the repeated-start form for some other reason, re-read that section first.
`pwm_pca9685_init()` also logs which specific step failed (register,
raw `S` value) via `uart_printf()` if any I2C call in the sequence fails —
see the comment on `pca9685_init_step_failed()`.

Only one PCA9685 instance is supported (single static `s_config`), matching
the original driver's scope — no handle/multi-instance support. Revisit if a
board ever needs two PCA9685 chips on different channels/addresses at once.

`pwm_pca9685_init()` calls `task_delay_ms()` (a real scheduler block, not a
busy-wait) for the datasheet's post-SLEEP-clear settle time, so it must run
in scheduled task context — not before `scheduler_start()`.

## Changes from the original

- **No `float`.** The original's `delay_percentage`/`duty_cycle` were
  `float`s (0.0-1.0), and the `PRE_SCALE` calculation used float division.
  This codebase has no `float`/`double` anywhere, and `CLAUDE.md` states no
  FPU context is saved across task switches — meaning float math in a
  preemptible task risks corrupting another task's FPU registers. All of it
  is now integer-only: `PRE_SCALE` via round-to-nearest integer division
  (`(numerator + denom/2) / denom`, mathematically equivalent to the
  original's `+0.5f` float rounding), and `pwm_pca9685_set_channel()`'s
  `delay`/`duty_cycle` are `uint32_t` fractions of `UINT32_MAX` scaled down
  to the 12-bit tick range via a 64-bit intermediate.
- **No `printf`/`usleep`.** Bare-metal, no libc. Debug prints (if ever
  needed) would go through `uart_printf()`; delays go through
  `task_delay_ms()` — see "Ownership and prerequisites" above.
- **Channel/address are runtime config, not `#define`s.** The original
  hardcoded `I2C_BUS_2` and `PCA_I2C_ADDR`. `Pca9685Config.channel`/
  `.i2c_addr` are now caller-supplied, matching `UartConfig`/`I2cConfig`'s
  pattern instead of baking one board's wiring into the driver source.
- **PWM output channels are a plain `0-15` index, not a register address.**
  The original's `PCAChannel` enum values *were* register addresses
  (`PCA_LED0_ON_L` etc.), leaked into the public API — callers did
  `channel + 1`, `channel + 3` arithmetic directly. This driver computes
  `PCA9685_LED0_ON_L + 4*pwm_channel` internally; callers just pass `0-15`.
- **Deinit stops all 16 channels, not a hardcoded 6.** The original's
  `PCA_DEINIT_CHANNEL` macro only covered channels 0-5, presumably matching
  one specific board's wiring. `pwm_pca9685_deinit()` here loops all 16 —
  cheap, and doesn't silently leave channels 6-15 outputting whatever they
  were last set to.
- **Fixed a discarded-error bug.** The original's `pwm_controller_stop_channel()`/
  `pwm_controller_digital_set_channel()` (`pwm_pca9685_set_channel_full_off()`/
  `_full_on()` here) did `ret = PCA_WRITE_REG(...); ret = PCA_WRITE_REG(...); return ret;`
  — the first write's result was immediately overwritten, so a failed first
  write with a successful second write reported success. Both functions here
  check and return on the first write's failure before attempting the second.
- **Renamed for clarity, no behavior change:**
  `pwm_controller_stop_channel`/`digital_set_channel` →
  `pwm_pca9685_set_channel_full_off`/`_full_on` (the original names didn't
  make clear which was on vs off). `PCA_DEFAULT_FREQ` (the 25 MHz *internal
  oscillator* clock, not a default PWM frequency) →
  `PCA9685_OSC_CLOCK_HZ`. `pwm_controller_get_initialized()` →
  `pwm_pca9685_is_initialized()`.

## Full-on/off tick semantics

`pwm_pca9685_set_channel()`'s `on_tick`/`off_tick` (after scaling) are
absolute positions in the 4096-tick period, not a start+width pair —
`off_tick < on_tick` is valid hardware behavior (a pulse that wraps the
period boundary: high late in the cycle, low early in the next), not
something this driver rejects. `LEDn_ON_H`/`LEDn_OFF_H` bit 4 is a separate
full-on/full-off override, independent of the tick value — that's what
`pwm_pca9685_set_channel_full_on()`/`_full_off()` set, per the datasheet's
documented sequence (clear the other override first, then set the one you
want).

## Open items

- `PCA9685_OSC_CLOCK_HZ` (25 MHz) assumes the internal oscillator
  (`MODE1.EXTCLK` clear) — this driver never sets `EXTCLK`, so that's
  self-consistent, but worth knowing if an external clock source is ever wired in.
- No interrupt/`ALLCALL`/sub-address support — phase 1 covers the direct
  per-channel PWM path only, matching the original driver's scope.
