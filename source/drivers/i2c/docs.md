# i2c

**Status: phase 1 implemented** — `i2c_channel_init`/`deinit`/`write`/`read`/
`write_read` are all in `i2c.c`. The rest of this doc was written
forward-looking, before the code landed; it's now a description of what's
actually there, not a plan.

One BSC (Broadcom Serial Controller) driver shared by every I2C channel on
the board, configured per-channel via `I2cConfig` — same shape as
`UartConfig`/`PL011Regs` in `uart/`: a fixed hardware descriptor table
(register base + IRQ) indexed by channel, plus a caller-owned config struct
that supplies the GPIO pins, ALT function, and bus speed.

## Channel table

The SoC has 6 BSC instances; the driver indexes by the literal BCM2711 I2C
number (`I2C_CHANNEL_0` = 0, `I2C_CHANNEL_1` = 1, `I2C_CHANNEL_3` = 3, …,
`I2C_CHANNEL_6` = 6). There is no general-purpose I2C2 — that block is
dedicated to HDMI on BCM2711 (`i2c@7ef04500`/`i2c@7ef09500`,
`brcm,bcm2711-hdmi-i2c`, a different driver's problem entirely) — so index 2
is a permanent, unsupported gap, same pattern as UART's mini-UART gap at
index 1.

All register bases, IRQ specs, and pin/ALT-function mappings below were
pulled directly from `zzdisc/bcm2711-rpi-cm4.dtb` (parsed by hand — no `dtc`
in this environment) rather than guessed from the datasheet, to avoid
repeating this project's history of silently-wrong register/pin assumptions.

| Channel | Reg base | Default pins | ALT | Status |
|---|---|---|---|---|
| `I2C_CHANNEL_0` (BSC0) | `0xFE205000` | GPIO0 (SDA0) / GPIO1 (SCL0) | ALT0 | Confirmed — phase 1 target |
| `I2C_CHANNEL_1` (BSC1, "i2c_arm") | `0xFE804000` | GPIO2 (SDA1) / GPIO3 (SCL1) | ALT0 | Confirmed — phase 1 target |
| `I2C_CHANNEL_3` (BSC3) | `0xFE205600` | GPIO4 (SDA3) / GPIO5 (SCL3) | ALT5 | Confirmed — phase 1 target |
| `I2C_CHANNEL_4` (BSC4) | `0xFE205800` | GPIO6 (SDA4) / GPIO7 (SCL4) | ALT5 | Placeholder — base filled in, pins unverified against datasheet |
| `I2C_CHANNEL_5` (BSC5) | `0xFE205A00` | GPIO10 (SDA5) / GPIO11 (SCL5) | ALT5 | Placeholder — base filled in, pins unverified against datasheet |
| `I2C_CHANNEL_6` (BSC6) | `0xFE205C00` | GPIO0 (SDA6) / GPIO22 (SCL6) | ALT5 | Placeholder — conflicts with I2C0 on GPIO0, needs care |

Each register block is `0x200` bytes. Placeholder rows follow the same rule
as UART's channels 2–4: filled in from the DTB so the table is complete, but
`i2c_channel_task_start()`-equivalent interrupt setup should refuse them
until cross-checked against the datasheet, same as `irq_intid == 0` does for
UART.

Several channels also have alternate pin positions in the DTB beyond the
table above (`i2c3-gpio2` in addition to `i2c3-gpio4`; `i2c0if-gpio28`/
`i2c0if-gpio44`; `i2c1-gpio44`/`i2c1-gpio46`) — not planned for phase 1.
`I2cConfig` carries the pins explicitly (see below) so a caller could still
pick one of these once verified; the hw table only fixes the register block
and IRQ per channel, not the pins, exactly like `UartConfig.tx_pin`.

### Shared interrupt line — needs verification before use

Every BSC node in the DTB (`I2C_CHANNEL_0`, `1`, `3`, `4`, `5`, `6`) lists the
**same** interrupt spec: SPI 117 → GIC INTID 149. If that's accurate on real
hardware, enabling interrupt-driven I2C on more than one channel at a time
means the shared ISR must check every active channel's status register to
find which one actually fired — the same shared-IRQ hazard class as the
watchdog/software-timer stack overflow in `CLAUDE.md`, just for interrupt
delivery instead of stack budget. **Do not wire up `gic_enable_spi(149, …)`
for I2C until this is confirmed against the BCM2711 ARM Peripherals
datasheet** — phase 1 is polling-only and doesn't touch this at all.

## Register block (`BSCRegs`)

Standard Broadcom BSC layout (same IP block across all Pi generations):

```c
typedef struct {
  volatile uint32_t C;      // 0x00 — control (I2CEN, INTR/INTT/INTD, ST, CLEAR, READ)
  volatile uint32_t S;      // 0x04 — status (TA, DONE, TXW, RXR, TXD, RXD, TXE, RXF, ERR, CLKT)
  volatile uint32_t DLEN;   // 0x08 — data length
  volatile uint32_t A;      // 0x0C — slave address
  volatile uint32_t FIFO;   // 0x10 — data FIFO (TX/RX)
  volatile uint32_t DIV;    // 0x14 — clock divider
  volatile uint32_t DEL;    // 0x18 — data hold/setup delay
  volatile uint32_t CLKT;   // 0x1C — clock stretch timeout
} BSCRegs;
```

## `I2cConfig`

Mirrors `UartConfig`'s shape and ownership rule — caller-owned,
static/global storage duration, one config pointer per channel in a
`s_i2c_configs[I2C_NUM_CHANNELS]` array set by `i2c_channel_init()`:

```c
typedef enum {
  I2C_BAUDRATE_STANDARD_100K,
  I2C_BAUDRATE_FAST_400K,
} I2cBaudrate;

typedef struct {
  uint8_t sda_pin;
  uint8_t scl_pin;
  GPIOFunc alt_func;
  I2cBaudrate baudrate;
} I2cConfig;
```

No `UART_MODE_BLOCKING`/`UART_MODE_BUFFERED_TASK`-style mode field yet —
phase 1 is blocking-only (see Phasing below). `gpio_set_pull()` is set to
`GPIO_PULL_UP` in `i2c_channel_init()` for both pins (I2C needs pull-ups to
work at all, unlike UART's `GPIO_PULL_NONE`) — but the DTB's
`brcm,pull = 2` is Linux's own internal-pull convention, and internal pulls
are weak; boards with long wires or multiple peripherals will likely still
need external pull-ups. Flag this in the header comment, not just here.

## Planned API (blocking, phase 1)

```c
StatusCode i2c_channel_init(uint8_t channel, I2cConfig *config);
void       i2c_channel_deinit(uint8_t channel);

StatusCode i2c_channel_write(uint8_t channel, uint8_t addr, const uint8_t *buf, uint16_t len);
StatusCode i2c_channel_read(uint8_t channel, uint8_t addr, uint8_t *buf, uint16_t len);

// Repeated-start register read: write() the register address without a stop,
// then read() len bytes — the standard "read register N" I2C idiom most
// peripherals expect. Needs the repeated-START (no STOP between the two
// halves), not two independent transfers.
StatusCode i2c_channel_write_read(uint8_t channel, uint8_t addr,
                                   const uint8_t *tx_buf, uint16_t tx_len,
                                   uint8_t *rx_buf, uint16_t rx_len);
```

Unlike UART there's no single canonical "default channel" (no `i2c_init()`/
`I2C_CHANNEL_PRINT`-style wrapper) — every call site names its channel
explicitly, since which bus a peripheral sits on is inherently
application-specific.

Transfer flow per call (polling, matching the classic BSC sequence): clear
`S` (write 1 to `CLKT`/`ERR`/`DONE`), set `A` to the 7-bit address, set
`DLEN`, fill/drain `FIFO` while polling `S.TXD`/`S.RXD`, set `C.ST` (and
`C.READ` for reads) to start, poll `S.DONE`, check `S.ERR` (NACK) and
`S.CLKT` (clock-stretch timeout) before returning `E_OK`.

## Repeated start (`i2c_channel_write_read`)

The BSC has **no hardware repeated-start** — the BCM2835/2711 I2C engine
doesn't support it natively. `i2c_channel_write_read()` fakes one by
re-arming `C.ST` while `S.TA` is still asserted from the write phase (i.e.
before any STOP has happened), which makes the engine chain a repeated
START into read mode instead of finishing the write with a STOP.

The technique is based on Mike McCauley's `bcm2835` C library —
`bcm2835_i2c_read_register_rs()`, the reference implementation used across
the RPi ecosystem for over a decade for exactly this "write register
address, read the value" idiom:

1. `C.CLEAR` (clear the FIFO), then `S = S_CLEAR_ALL`.
2. `A = addr`, `DLEN = tx_len`, `C = C_I2CEN` (enabled, no `ST` yet).
3. **Pre-fill the FIFO with the whole tx payload before setting `ST`** — legal
   because the FIFO accepts writes once `I2CEN` is set, even pre-transfer.
4. `C = C_I2CEN | C_ST` — kick off the write phase.
5. **Poll for `S_TA`** (with `S_DONE` as a fast-path fallback, for a write
   phase so short it finishes before the poll observes `TA`) — this is the
   actual hand-off point: once `TA` confirms the START + address are
   committed to hardware, it's safe to reprogram for the read phase.
6. `DLEN = rx_len`, then `C = C_I2CEN | C_ST | C_READ` — `ST` again while
   `TA` is still 1 chains a repeated START instead of a STOP.
7. Drain the RX FIFO exactly like `i2c_channel_read()`.

**Known limits — not equally verified:**
- The pre-fill step only works because `tx_len` fits in the 16-byte FIFO in
  one shot (`I2C_FIFO_DEPTH`); `i2c_channel_write_read()` returns
  `E_INVALID_ARGS` above that. Extending this to a fill-loop for a write
  phase that doesn't fit in one FIFO load — feeding `FIFO` via `S_TXD`
  polling *while also* watching for the `TA` hand-off — is a reasonable
  extrapolation from the verified pattern, not something with a citable
  reference implementation. Don't trust it without testing on real hardware
  first if this limit is ever lifted.
- Broadcom's own engineer has publicly acknowledged the BSC's
  clock-stretching implementation is flawed outside the ACK phase. A slave
  that stretches the clock during exactly this write→read hand-off window
  can produce unreliable results — this is why the Linux kernel's own
  `i2c-bcm2835` driver keeps its equivalent "combined transactions" feature
  opt-in rather than default. Worth keeping in mind if a repeated-start
  transfer misbehaves on real hardware — it may not be this driver's bug.

## Open items — verify before implementing

- **BSC core clock frequency for CM4.** `DIV` is computed from the BSC's
  input clock, not a fixed constant like `UART_CLK` — the DTB only points at
  a clock-manager phandle, not a literal Hz value. Get the real number via
  the mailbox `GET_CLOCK_RATE` property tag (clock id for I2C) at runtime,
  or confirm a fixed value against the datasheet — don't hardcode a
  remembered number from a different Pi model without checking.
- Confirm GIC INTID 149 is really shared across all six BSC blocks (see
  above) before any interrupt-driven phase.
- I2C4/5/6 pin/ALT mappings (placeholder rows) — cross-check against the
  BCM2711 ARM Peripherals datasheet, same bar UART held its channel 2–4
  placeholders to before ever wiring one up.
- GPIO0/1 (I2C0) is conventionally reserved for the HAT ID EEPROM on
  Raspberry Pi boards — the driver won't enforce that, but it's worth a
  comment so a future caller doesn't collide with EEPROM probing if that's
  ever added.

## Phasing

1. **Phase 1 (this plan's scope):** blocking/polling driver, three confirmed
   channels — `I2C_CHANNEL_0` (GPIO0/1, ALT0), `I2C_CHANNEL_1` (GPIO2/3,
   ALT0), `I2C_CHANNEL_3` (GPIO4/5, ALT5).
2. **Phase 2 (future):** interrupt-driven transfers, once GIC INTID 149's
   shared-across-channels behavior is confirmed on real hardware.
3. **Phase 3 (future):** promote `I2C_CHANNEL_4`/`5`/`6` from placeholder to
   confirmed once their pin/ALT mappings are checked against the datasheet.
