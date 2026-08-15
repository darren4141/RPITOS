# cprman

**Status: phase 1 implemented** — `cprman_pcm_clock_enable`/`disable`/
`is_running`/`status` are in `cprman.c`. Written to unblock `i2s/` (the PCM
clock generator is the only clock source for PCM_CLK when the CM4 is I2S bus
master — see `i2s/docs.md`).

`cprman_pcm_clock_status()` is a debug aid, added during `i2s_stream_demo`'s
first real-hardware bring-up when the achieved sample throughput came back
~2.8x the configured target (see `i2s/docs.md`'s Open items). It reads back
the raw `CTL`/`DIV` registers so a caller can confirm what actually got
latched into hardware — ruling out "the divisor write silently didn't take"
as an explanation. It does **not** confirm PCM_CLK is actually toggling at
the resulting rate on the pin; only a scope/logic analyzer can do that.

Scoped to the PCM clock generator only, not a general CPRMAN driver. CPRMAN
manages ~20 clock generators on this SoC (PWM, UART, EMMC, GPCLK0-2, VEC,
...) sharing the same `CM_xxxCTL`/`CM_xxxDIV` register format — this driver
only fills in PCM's offset because nothing else needs one yet. Adding
another clock later means adding another offset constant and (if it needs a
different source than the oscillator) extending `CprmanClockSrc`-style
source support — not restructuring this file.

## Source of the register layout

Unlike `i2c/`'s DTB-derived pin table, there's no per-board DTB fact to pull
here — `CM_PCMCTL`/`CM_PCMDIV`'s bit layout is a SoC-level constant, not a
board wiring choice. Pulled from `raspberrypi/linux`'s
`drivers/clk/bcm/clk-bcm2835.c` (the mainline BCM2835/2711 clock driver)
rather than reconstructed from a secondary source, to hold the same bar
CLAUDE.md's "Known Issues" history sets for this project: several past bugs
here were "silently wrong register assumption" bugs, not typos.

Confirmed against that source:
- `CM_PCMCTL` offset `0x098`, `CM_PCMDIV` offset `0x09C` (from `CPRMAN_BASE`)
  — adjacent words, modeled as one `CprmanClockRegs` struct.
- `CM_PASSWORD = 0x5A000000` — required in bits[31:24] of every write to any
  `CM_xxxCTL`/`CM_xxxDIV` register or the write is silently dropped.
- `CTL` bit positions: `SRC[3:0]`, `ENAB` bit 4, `KILL` bit 5, `BUSY` bit 7
  (read-only), `MASH[10:9]`.
- `DIV` format: `DIVI[23:12]` (12-bit integer divisor), `DIVF[11:0]` (12-bit
  binary fraction, 1/4096 units) — a native fixed-point format, which is why
  `cprman_pcm_divisor()` computes it with a `<< 12` shift instead of float math
  (no FPU state saving in this kernel — see CLAUDE.md's Key Constraints).
- Oscillator source (`SRC = 1`) is a fixed 19.2 MHz crystal — the only source
  with a rate this driver can know without a mailbox round-trip.

**Not verified against real hardware yet** — this is the Linux kernel's own
register model for this exact SoC family (BCM2835 through BCM2711 share
CPRMAN's core design), so confidence is high, but no CM4 board has actually
exercised this code. Treat the divisor math and the enable/disable sequence
as needing an oscilloscope/logic-analyzer check on PCM_CLK before trusting
audio output — the same bar `i2c/docs.md` held BSC0/1/3's pin table to
before calling it "confirmed."

## Why the oscillator, not PLLD_PER

`SRC = 6` (PLLD_PER) is the standard choice for 44.1 kHz-family audio in
Linux — it runs at a rate that divides more cleanly into 44.1 kHz multiples
than the 19.2 MHz oscillator does, meaning less fractional-divider error.
Not used here because there's no way to query its actual running rate yet:
`mailbox.h`'s `MBOX_CLOCK_ID_*` list has no PLLD_PER entry, and its rate
isn't a documented fixed constant the way the oscillator's is (it can move
depending on `config.txt`/firmware). Hardcoding a remembered PLLD_PER value
without a way to verify it at runtime is exactly the class of mistake this
driver is trying to avoid — see the oscillator-only choice as a deliberate
accuracy-over-headroom tradeoff for phase 1, not an oversight.

**Consequence**: the oscillator doesn't divide evenly into standard audio bit
clocks even with MASH fractional correction — e.g. target 1,536,000 Hz (48 kHz
× 32-clock frame) divides as 19,200,000 / 1,536,000 = 12.5 exactly (clean),
but the 44.1 kHz family doesn't land on as clean a fraction. MASH stage 1
keeps the *average* rate close to target (the whole reason MASH dividers
exist), but the instantaneous cycle-to-cycle jitter this introduces hasn't
been measured on a scope. If sample audio comes out audibly wrong (pitch or
noise floor), this is the first thing to check — not necessarily `i2s.c`.

## Open items — verify before trusting for real audio work

- **MASH stage choice (`CPRMAN_PCM_MASH_STAGE = 1`)** is a reasonable default,
  not a verified one — higher stages trade more jitter for better averaged
  accuracy over more cycles. Not benchmarked against this driver's actual
  target rates.
- **Enable/disable timing.** The busy-wait timeouts (`100000` iterations) are
  arbitrary, same caveat every other driver's polling timeout in this
  codebase carries (see `i2c/docs.md`'s equivalent note) — not calibrated
  against real settle time from a datasheet or a scope trace.
- **KILL as a disable fallback** stops the generator immediately mid-cycle.
  If `i2s.c` or a future caller is actively clocked from this generator when
  that fires, expect a glitch on PCM_CLK, not a clean stop. Only exercised
  today via `cprman_pcm_clock_enable()`'s own disable-before-reconfigure call,
  where the peripheral downstream should already be idle.

## Phasing

1. **Phase 1 (this plan's scope):** PCM clock generator, oscillator source
   only, integer target-Hz API (`cprman_pcm_clock_enable(uint32_t target_hz)`).
2. **Phase 2 (future):** PLLD_PER source, once there's a runtime way to query
   its actual rate (a mailbox tag, if one exists for it, or a documented fixed
   value confirmed against the datasheet).
