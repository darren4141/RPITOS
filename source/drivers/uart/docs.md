# uart

PL011 UART0 driver. Two build modes, selected by `UART_MINIMAL`:

- **Full mode** (default): ring-buffer TX drained by a scheduler task, RX by
  interrupt, DFU-trigger watch built in.
- **Minimal mode** (`UART_MINIMAL` defined, used by the bootloader): blocking
  TX, no task, no ring buffer, no RX interrupt.

## Multicore

`uart_send_byte()`/`uart_print()`/`uart_printf()` are safe to call from any
core once `uart_task_start()` has run on the (single, still core-0-owned)
UART hardware. The ring buffer's reserve-a-slot-and-write step in `uart_tx()`
is guarded by its own `Spinlock` (`uart_buf_lock`), nested inside the
existing `enter_critical()` — `enter_critical()` alone only stops same-core
preemption, so without the spinlock two cores producing concurrently would
race `p_uart_buf_right`/`uart_buf[]`. `semaphore_give(&uart_data_ready)`
wakes `uart_tx_task` correctly regardless of which core produced the bytes,
since `Semaphore` is itself cross-core safe — see `spinlock/docs.md`.

## Build-time flags (full mode only)

- `UART_TX_DMA` (default `1`): TX path selector. `1` drives TX via DMA Lite
  channel 7; `0` falls back to classic byte-by-byte PIO draining (spins on
  `FR_TXFF` per byte). Build with `-DUART_TX_DMA=0` to A/B the two paths.
- `UART_TX_TIMING` (default `0`): when `1`, accumulates the CPU-active cycles
  the TX task spends pushing bytes, independent of `UART_TX_DMA` so all four
  combinations (DMA/PIO × timing on/off) can be measured. Build with
  `-DUART_TX_TIMING=1`.

## Bus addressing

`UART0_DR_BUS` (`0x7E201000`) is the VideoCore bus alias of `UART0->DR`, used
because the DMA controller addresses peripherals via `0x7Exxxxxx`, not the
ARM-physical `0xFExxxxxx` that the CPU uses.

## Interrupt wiring

If `UART_DMA_TX_CHANNEL` or `UART_IRQ_INTID` change, the matching
`cmp r2, #<intid>` dispatch lines in `startup/startup.s` must be updated to
match (INTID = 112 + channel for DMA; 153 is the PL011 combined IRQ on
BCM2711).

## Telemetry UART (`RTOS_TELEMETRY` builds only)

A second, dedicated PL011 instance — UART3 (`0xFE201600`), TXD3 on GPIO4
(`ALT4`), TX-only (RXD3/GPIO5 left unconfigured). Kept separate from UART0 so
telemetry framing never has to resync around interleaved `uart_print()`/
`uart_printf()` console traffic — see `md/client/transport_protocol.md`.

`uart_telemetry_init()` configures it for 921600 baud (`IBRD=3, FBRD=16` at
the same `UARTCLK = 48 MHz` reference as `UART_IBRD_115200`/`UART_FBRD_115200`
above). `uart_telemetry_tx_raw()` is a raw, blocking single-byte write — no
ring buffer, no DMA, no TX task, unlike the full-mode UART0 path. Acceptable
because the only caller, `telemetry_publisher_task`, runs alone on a
dedicated core (see `telemetry/docs.md`).
