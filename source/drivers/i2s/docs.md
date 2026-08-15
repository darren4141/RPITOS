# i2s

**Status: phase 1 implemented** — `i2s_init`/`deinit`/`transfer` are in
`i2s.c`: blocking, polling-only, full-duplex, 16-bit stereo, CM4 as I2S bus
master (generates PCM_CLK/PCM_FS itself). Written forward-looking, before
hardware testing — see "Open items" before trusting this against real audio
hardware.

Single PCM peripheral, unlike `i2c/`/`uart/`'s multiple channel instances —
the BCM2711 has exactly one PCM/I2S block, so there's no channel table, no
per-channel `s_configs[]` array, and no `i2s_channel_*()` naming. Otherwise
the shape deliberately mirrors `UartConfig`/`I2cConfig`: a caller-owned,
static/global `I2sConfig` supplying pins + ALT function + the one tunable
(sample rate), consumed by a single `i2s_init(config)` call — same pattern
`pwm_pca9685.c` uses for its single-instance `s_config` pointer.

## Source of the register layout

Pulled from `raspberrypi/linux`'s `sound/soc/bcm/bcm2835-i2s.c` (the mainline
ALSA driver for this exact peripheral) rather than reconstructed from the
BCM2835 datasheet by hand, for the same reason `cprman/docs.md` cites: this
project's history (CLAUDE.md's "Known Issues") is full of bugs from silently
wrong register/bit assumptions, and a real, shipping Linux driver for the
identical hardware block is a stronger source than a re-derivation.

Confirmed against that source:
- Register offsets `CS_A`=0x00, `FIFO_A`=0x04, `MODE_A`=0x08, `RXC_A`=0x0C,
  `TXC_A`=0x10, `DREQ_A`=0x14, `INTEN_A`=0x18, `INTSTC_A`=0x1C, `GRAY`=0x20.
- `CS_A` bit positions: `EN`=0, `RXON`=1, `TXON`=2, `TXCLR`=3, `RXCLR`=4,
  `TXTHR`=[6:5], `RXTHR`=[8:7], `DMAEN`=9, `TXSYNC`=13, `RXSYNC`=14,
  `TXERR`=15, `RXERR`=16, `TXW`=17, `RXR`=18, `TXD`=19, `RXD`=20, `TXE`=21,
  `RXF`=22, `RXSEX`=23, `SYNC`=24, `STBY`=25.
- `MODE_A`: `FSLEN`=[9:0], `FLEN`=[19:10], `FSI`=20, `FSM`=21, `CLKI`=22,
  `CLKM`=23, `FTXP`=24, `FRXP`=25, `PDME`=26, `PDMN`=27, `CLKDIS`=28.
- `TXC_A`/`RXC_A`: each register packs two 16-bit channel slots
  (`CH1` = bits[31:16], `CH2` = bits[15:0]); within a slot, `WID`=[3:0],
  `POS`=[9:4], `EN`=14, `WEX`=15.
- `DREQ_A`: `RX`=[7:0], `TX`=[15:8], `RX_PANIC`=[23:16], `TX_PANIC`=[31:24].
  Defined in the header for completeness (matches `uart.h`'s `DMACR` bits
  being defined even before DMA was wired up) but unused until phase 2.
- **Master-mode bit polarity** — confirmed from the driver's actual
  `hw_params()` logic, not just the bit-position table: `CLKM`/`FSM` are both
  **clear** (0) when the BCM2835/2711 is the bus master (drives PCM_CLK/
  PCM_FS itself), not set. This is the opposite of what the field names might
  suggest at a glance ("clock mode" reads ambiguous either way) — worth
  double-checking against the source again if this ever gets refactored.
- **Standard I2S framing** (not left-justified, not raw PCM/DSP mode) needs
  `CLKI=1`, `FSI=1`, and a **1 PCM_CLK-cycle data delay** between the FS edge
  and each channel's first data bit — this delay is what distinguishes I2S
  from left-justified format on this peripheral. `i2s.c` bakes this in as
  `I2S_DATA_DELAY = 1`, added to both `CH1_POS`/`CH2_POS`.
- Channel positions follow the driver's own formula: `pos = slot_index *
  slot_width + data_delay`. For 16-bit stereo (`slot_width` = 16):
  `CH1_POS = 1`, `CH2_POS = 17`, giving a compact 32-PCM_CLK-cycle frame
  (`FLEN` = 31, `FSLEN` = 16 for a 50% FS duty cycle) — not the 64-cycle
  frame some DACs' datasheets show as an example; both are valid I2S, this
  just matches what the reference driver actually emits.

**Not verified against real hardware.** No CM4 board has run this code, no
oscilloscope has confirmed PCM_CLK/PCM_FS timing, and no codec/DAC has been
looped back to confirm sample alignment. Confidence in the register bits
themselves is high (real shipping driver, same silicon family); confidence
in *this driver's specific sequencing* (settle delays, FIFO clear timing) is
much lower. Treat this the way `i2c/docs.md` treated BSC4/5/6 before they
were checked against the datasheet: "placeholder," not "confirmed," until
someone puts a scope on the pins.

## Pins

Standard I2S pins on the CM4 40-pin header, ALT0: PCM_CLK=GPIO18,
PCM_FS=GPIO19, PCM_DIN=GPIO20, PCM_DOUT=GPIO21 (`I2S_PIN_CLK`/`_FS`/`_DIN`/
`_DOUT` in `i2s.h`). High confidence — this is the one fixed, universally
documented I2S pin group on every 40-pin-header Pi model, not something that
varies like the I2C channel table did.

## Full-duplex FIFO servicing (`i2s_transfer`)

`i2s_init()` always enables both `TXON` and `RXON` — there's no TX-only/
RX-only config flag yet (see Phasing). Once enabled, the PCM engine
continuously clocks PCM_CLK/PCM_FS and samples/drives every frame slot
regardless of whether software is currently servicing the FIFOs — this is
just how a running I2S bus master behaves, not something this driver
controls per-call.

That's why `i2s_transfer()` takes both a `tx` and an `rx` buffer and
services whichever FIFO is ready in the same polling loop, instead of being
two independent blocking calls: a hypothetical `i2s_write()` that blocks
until `count` TX samples are pushed, called back-to-back with a separate
`i2s_read()`, would leave the RX FIFO unattended for the whole TX call —
with `RXON` continuously sampling, `RXF` (FIFO full) and dropped/corrupted
samples are the likely result on anything but a very short transfer. This
mirrors the same class of hazard CLAUDE.md's watchdog/software-timer
postmortem describes (a resource that keeps moving whether or not something
is there to service it), just for a FIFO instead of a stack.

A caller that only cares about one direction still has to pass a real buffer
for the other (there's no `NULL`-means-skip today — see Phasing).

## FIFO clear timing

`TXCLR`/`RXCLR` are pulsed with a short spin-wait (`I2S_CLEAR_SETTLE_SPINS`,
arbitrary, uncalibrated) before being cleared again in `i2s_init()`. This is
based on the general pattern (seen across several bare-metal I2S
implementations for this SoC family) that a FIFO clear needs at least a
couple of PCM_CLK cycles to actually take effect before it's safe to
re-arm — not something confirmed against the datasheet's exact timing
figure. If `i2s_init()` produces a stuck or garbage first frame, this is
worth revisiting before assuming the bug is elsewhere.

## Debug aids

`i2s_last_status()` (raw `CS_A` as of the last `i2s_transfer()` poll
iteration), `i2s_transfer_totals()` (cumulative sample counts moved in each
direction since `i2s_init()`), and `i2s_error_total()` exist purely for
callers doing hardware bring-up — same role `i2c_channel_last_status()`
plays for `i2c/`. A monotonically increasing `tx_total`/`rx_total` across
repeated calls is the simplest available signal that `FIFO_A` accesses are
actually happening; `i2s_stream_demo` prints all three periodically for
exactly this reason.

**`i2s_error_total()` is not purely passive** — unlike the other two, it has
a real side effect: `i2s_transfer()` clears `CS_A.TXERR`/`RXERR` (write-1-to-
clear "wrote to TX FIFO while full" / "read from RX FIFO while empty"
latches) once per call after observing them set, specifically so this
counter reflects *distinct occurrences since init*, not "stuck at 1 forever
after the first one ever happened." The clearing write reasserts
`I2S_CS_A_RUN_BITS` (`EN`/`TXON`/`RXON`/`TXTHR`/`RXTHR`) alongside the W1C
bits — `CS_A` packs level/config state and W1C event bits in the same
register, so a write that set only the error bits would silently zero out
`TXON`/`RXON` and stop the engine. `i2s_init()`'s own final `CS_A` write does
the same thing for the same reason (clearing whatever the `TXCLR`/`RXCLR`
pulse earlier in `i2s_init()` may have latched, so every fresh init starts
from a known-clean error count).

**First real hardware run turned up `TXERR`/`RXERR` latched in every status
line** before this clear-on-detect logic existed — i.e. before this, there
was no way to tell whether that was a one-time init artifact or a live,
recurring condition. That ambiguity is exactly why the clearing logic and
`i2s_error_total()` were added; whether the count climbs during a real run
is now an open, trackable question instead of a permanently stuck bit — see
Open items.

## Open items — verify before trusting for real audio work

- **Everything under "Not verified against real hardware" above** — no
  substitute for actually probing PCM_CLK/PCM_FS with a scope/logic analyzer
  once this runs on the CM4.
- **`CS_A_RXSEX`** (RX sign extension) isn't set — for 16-bit RX samples this
  should be harmless (the peripheral shouldn't need to sign-extend a width
  that already matches the FIFO's natural interpretation), but hasn't been
  checked against a real negative-sample RX capture.
- **FIFO packing at 16-bit width.** Some descriptions of this peripheral
  suggest the FIFO can pack two ≤16-bit samples into one 32-bit `FIFO_A`
  access. This driver does **not** assume or rely on that — every
  `i2s_transfer()` FIFO access is exactly one channel-slot sample, one
  `FIFO_A` read/write each. If real hardware turns out to require or benefit
  from packed access at this width, that's a deliberate follow-up, not
  something silently assumed here.
- **`cprman`'s divisor accuracy** for the 44.1 kHz family — see
  `cprman/docs.md`'s "Why the oscillator, not PLLD_PER." If a captured tone
  sounds pitch-shifted, check the actual PCM_CLK frequency before assuming
  `i2s.c`'s frame math is wrong.
- **Unresolved as of the first `i2s_stream_demo` hardware run**: with
  `i2s_error_total()` and the throughput diagnostic both in place, a real
  CM4 run showed `errors` climbing steadily (not a one-time init artifact —
  real, recurring `TXERR`/`RXERR`) and achieved throughput ~2.8x the
  configured 48 kHz target, both stable over 10,000+ transfers. Two open
  hypotheses, not yet distinguished: (1) `cprman_pcm_clock_enable()`'s
  computed divisor isn't what's actually driving PCM_CLK (though
  `cprman_pcm_clock_status()`'s register readback shows the intended
  DIVI=12/DIVF=2048 *is* latched — so if this is the cause, the bug is in
  how CPRMAN turns that divisor into a real signal, not in the software's
  arithmetic), or (2) `CS_A.TXD`/`RXD` aren't being gated by real per-frame
  hardware timing at all, in which case the achieved-throughput figure is
  actually measuring CPU/MMIO polling speed, not audio rate, and the errors
  come from a different mechanism entirely. The next test that
  distinguishes them costs nothing to run: change the configured sample
  rate and see whether achieved throughput scales proportionally (points at
  (1)) or stays pinned near the same absolute number regardless of target
  (points at (2)). Don't change `MODE_A`/`CS_A`/divisor register values
  based on a guess before that test (or a scope on PCM_CLK) narrows it down.

## Phasing

1. **Phase 1 (this plan's scope):** blocking/polling, full-duplex, fixed
   16-bit stereo, CM4 as bus master, oscillator-sourced PCM clock.
2. **Phase 2 (future):** DMA-paced continuous streaming. This needs more
   than swapping the FIFO access for a DMA control block — sustained audio
   needs double-buffered/chained control blocks so the FIFO never
   underruns/overruns while a task refills the other half, a different DMA
   usage pattern than `dma/`'s current single-shot-CB precedent
   (`uart.c`'s TX DMA). `DREQ_A`'s TX/RX panic+threshold fields and the PCM
   TX/RX DREQ IDs (2 and 3 respectively, per `raspberrypi/linux`'s
   `drivers/dma/bcm2835-dma.c` device-tree bindings — not yet added to
   `dma.h`'s `DMA_DREQ_*` list) are reserved for this phase, not wired up yet.
3. **Phase 3 (future):** TX-only/RX-only mode (skip enabling the unused
   direction's `TXON`/`RXON` and drop the NULL-buffer restriction on
   `i2s_transfer()`), wider sample depths (24/32-bit, using `CHAN_WEX`), and
   slave mode (external codec supplies PCM_CLK/PCM_FS — skips `cprman`
   entirely, see the parent conversation's feasibility notes).
