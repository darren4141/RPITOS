# i2s_stream_demo

**Bring-up/liveness check for `i2s/` + `cprman/`, not an audio quality
demo.** Every register value in both drivers was pulled from the Linux
kernel's driver source for this exact SoC (see `i2s/docs.md`/`cprman/docs.md`
for citations) but neither has run on real CM4 hardware yet. This sample's
only job is to actually call the driver on the board so a real failure shows
up as a clear UART message instead of the code path just never being
exercised.

A single task fills a small buffer with a continuous ~440 Hz square wave
(cheap integer math, not a real sine — just an audible/scope-visible
liveness signal) and calls the existing blocking `i2s_transfer()` API in a
tight forever loop, full-duplex.

## Optional: MAX98357A amp wiring

If you're driving a MAX98357A (or similar I2S Class-D amp) rather than just
scoping the pins:

| Pi | MAX98357A |
|---|---|
| 3.3V or 5V | VIN |
| GND | GND |
| GPIO18 (PCM_CLK) | BCLK |
| GPIO19 (PCM_FS) | LRC |
| GPIO21 (PCM_DOUT) | DIN |
| **GPIO16** | **SD** |
| — | GAIN (leave floating — 9dB default) |

`SD` (`SD_MODE`) is the amp's shutdown pin, not part of the I2S protocol —
below 0.16V the amp is fully muted regardless of what's on the bus, which is
the #1 cause of "everything looks right but I hear nothing." `kmain()`
drives GPIO16 high right after `i2s_init()` returns `E_OK` (and explicitly
low if it fails, so a broken bring-up doesn't leave the amp live with no
real clock) — deliberately kept in `main.c`, not `i2s.c`, since SD_MODE is
specific to this one downstream chip, not the I2S bus itself (same
separation `i2c.c`/`pwm_pca9685.c` already draw).

## What to watch for

- **`E_TIMED_OUT` on the transfer** means `CS_A.TXD`/`RXD` never asserted —
  i.e. PCM_CLK isn't actually toggling. That points at
  `cprman_pcm_clock_enable()` or `i2s_init()`'s `MODE_A`/`CS_A` setup, not
  necessarily the transfer loop itself. This sample prints it immediately,
  every time it happens, not just in the periodic status line.
- **The periodic status line**
  (`iter=... timeouts=... tx_total=... rx_total=... rx[0]=... rx[1]=...
  CS_A=0x........`) every 100 iterations. `tx_total`/`rx_total` (from
  `i2s_transfer_totals()`) are the real "are transactions going through"
  signal — they only climb when `FIFO_A` accesses actually happen, unlike
  the iteration count, which climbs on every call regardless of outcome.
  `CS_A` (from `i2s_last_status()`) is the raw register if the totals alone
  don't explain what's happening — decode against the `CS_A_*` bitmasks in
  `i2s.h`.
- **Optional loopback**: bridge `PCM_DOUT` (GPIO21) to `PCM_DIN` (GPIO20)
  with a jumper wire before boot. If the peripheral is framing correctly,
  the tone written to `tx_buf` should show up in `rx_buf` (visible in the
  `rx[0]`/`rx[1]` status values) — not a bit-exact/timing-validated check,
  just a "does something come back that isn't silence" sanity signal. With
  no jumper, `rx[]` just reflects whatever's floating on DIN, which is
  expected and not a failure.
- **Scope/logic analyzer**: if available, PCM_CLK (GPIO18) and PCM_FS
  (GPIO19) should show a steady clock and a 50%-duty frame-sync square wave
  at `sample_rate * 32` Hz (48 kHz × 32 = 1.536 MHz here) the whole time this
  sample runs.

## Build

```bash
make SAMPLE=rtos/i2s_stream_demo
```

Output: `build/rtos/i2s_stream_demo/i2s_stream_demo.elf / .img / .hex`

## Components used

- **Drivers**: `gpio`, `uart`, `gentimer`, `gic`, `jtag`, `reset`, `interrupts`, `dma`, `watchdog`, `emmc`, `crc`, `cprman`, `i2s`
- **Kernel**: `scheduler`, `task`, `semaphore`, `heap`, `software_timer`
- **Boot library**: `dfu_trigger`, `boot_flags`

Same baseline skeleton `full_demo`/`pwm_fade_demo` use (watchdog + A/B
trial-boot confirm, DFU trigger via the buffered UART task) — no I2C/mailbox
here, unlike `pwm_fade_demo`, since nothing in this sample needs them.
