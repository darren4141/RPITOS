# uart

One PL011 driver shared by every UART channel on the board (print console,
telemetry, or any future channel), configured per-channel via `UartConfig`.
Two build modes, selected by `UART_MINIMAL`:

- **Full mode** (default): a channel configured with `UART_MODE_BUFFERED_TASK`
  gets ring-buffer TX drained by a scheduler task, RX by interrupt, DFU-trigger
  watch built in (print channel only, today).
- **Minimal mode** (`UART_MINIMAL` defined, used by the bootloader): every
  channel is always blocking TX — no task, no ring buffer, no RX interrupt,
  regardless of what `mode` a config asks for.

## Channel table

The SoC has 6 UART slots; the driver indexes by the literal BCM2711 UART
number (`UART_CHANNEL_PRINT` = 0, `UART_CHANNEL_TELEMETRY` = 3). Index 1 is
the mini-UART — a completely different, non-PL011 register layout — and is a
permanent, unsupported gap (`uart_channel_init(1, ...)` returns `E_NOTSUPP`).
Base addresses for channels 2/4/5 are filled in (spaced `0x200` apart from
UART0/UART3) but are placeholders: nothing wires them up or exercises them
today, and their combined IRQ INTIDs are left `0` (unknown) — verify against
the datasheet before ever using one. `uart_channel_task_start()` refuses
RX-interrupt setup on a channel whose `irq_intid` is `0`.

`UartConfig` is caller-owned, static/global storage duration — same
pointer-ownership rule as `WatchdogConfig` (see `watchdog/docs.md`). Two
distinct configurations of the same struct type produce very different
runtime behavior: the print channel typically sets
`.mode = UART_MODE_BUFFERED_TASK` (ring buffer + semaphore + task, optionally
DMA), while telemetry sets `.mode = UART_MODE_BLOCKING` (always direct MMIO,
same as `uart_tx_raw()`) — see `telemetry/docs.md` for why telemetry
deliberately never buffers.

Only ONE channel may be in `UART_MODE_BUFFERED_TASK` at a time — the ring
buffer, semaphores, and TX task are a single shared backend (not a per-channel
array; a full 6-way array would waste ~12 KB of ring buffer for channels
nobody buffers). `uart_channel_task_start()` returns `E_RESOURCE_EXHAUSTED`
if a different channel already owns it.

## Multicore

`uart_channel_send_byte()`/`uart_channel_print()`/`uart_channel_printf()` (and
their print-channel wrappers `uart_send_byte()`/`uart_print()`/`uart_printf()`)
are safe to call from any core once `uart_channel_task_start()` has run for
that channel. The ring buffer's reserve-a-slot-and-write step in
`uart_channel_tx()` is guarded by its own `Spinlock` (`uart_buf_lock`), nested
inside the existing `enter_critical()` — `enter_critical()` alone only stops
same-core preemption, so without the spinlock two cores producing concurrently
would race `p_uart_buf_right`/`uart_buf[]`. `semaphore_give(&uart_data_ready)`
wakes the TX task correctly regardless of which core produced the bytes, since
`Semaphore` is itself cross-core safe — see `spinlock/docs.md`.

## DMA

`UartConfig.is_dma_enabled` is a runtime choice per channel (the old
`UART_TX_DMA` compile-time flag is retired) — `1` drives TX via DMA Lite on
`UartConfig.dma_channel`; `0` falls back to classic byte-by-byte PIO draining
(spins on `FR_TXFF` per byte). The DMA TX code always compiles into non-
`UART_MINIMAL` builds now; whether a given channel actually uses it is purely
config. `DMA_DREQ_UART_TX` is currently only verified correct for UART0's TX
DREQ line — don't set `is_dma_enabled` on another channel without checking
its DREQ number first.

`UART_TX_TIMING` (default `0`, full mode only) still a compile-time flag: when
`1`, accumulates the CPU-active cycles the TX task spends pushing bytes.
Build with `-DUART_TX_TIMING=1`.

## Bus addressing

DMA's `dest_ad` is computed per-channel via `PERIPHERAL_BUS_ADDRESS(&regs->DR)`
(see `dma.h`) — the VideoCore bus alias of a channel's `DR` register, needed
because the DMA controller addresses peripherals via `0x7Exxxxxx`, not the
ARM-physical `0xFExxxxxx` that the CPU uses. `source_ad` (the ring buffer)
uses the separate `BUS_ADDRESS()` macro instead — that one's the RAM alias
(`0xC0000000`-based), a different transform than the peripheral one. Using
`BUS_ADDRESS()` for a peripheral register (or vice versa) computes the wrong
bus address silently — no fault, DMA just writes nowhere useful. This is
exactly the bug this generalization introduced and fixed once: `dest_ad` was
briefly `BUS_ADDRESS(&regs->DR)`, which broke print-channel DMA TX while
telemetry (always blocking, never DMA) kept working — the giveaway that made
it findable.

## Interrupt wiring

DMA-TX-complete and UART RX interrupts go through the generic C dispatch
table (`irq_register()`/`irq_dispatch()` in `drivers/interrupts`), registered
at `uart_channel_task_start()` time for whatever INTID the channel's
`dma_channel`/`irq_intid` actually resolve to — `startup.s`'s assembly only
special-cases GIC ID 30 (the scheduler tick) directly; everything else,
including DMA TX and UART RX, is a runtime table lookup. No `startup.s`
changes are needed when a channel's DMA channel or IRQ INTID changes.
