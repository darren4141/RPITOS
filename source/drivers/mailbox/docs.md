# mailbox

VideoCore mailbox, property-tag channel (channel 8) — the standard ARM<->VC
RPC mechanism on BCM283x/2711: clock rates, memory splits, board info,
framebuffer setup, and more, all through one request/response protocol.
Extracted out of `i2c/` (its first caller, for the BSC core clock rate)
rather than left inline, since the mailbox is its own hardware block, same
reasoning as `dma/` being separate from `uart/` even with one caller.

## Wire format

A property-tag message is a flat `uint32_t` buffer, 16-byte aligned:

```
[0] total buffer size, bytes
[1] request code (0 = request; VC overwrites with 0x80000000 on success)
[2] tag ID (e.g. MBOX_TAG_GET_CLOCK_RATE)
[3] tag value buffer size, bytes (must fit the larger of request/response)
[4] request/response indicator (0 = request; VC ORs in 0x80000000 + actual
    response length on success)
[5..] tag value words — request payload going in, response payload coming back
[N]  0x00000000 end tag (repeat [2]-[N] for multiple tags in one call)
```

Send it by writing `BUS_ADDRESS(buf) | MBOX_CH_PROPERTY` to `MBOX_WRITE`
(`0xFE00B880 + 0x20`), then poll `MBOX_READ` (`+0x00`) until a value equal to
what was written comes back — the buffer's been overwritten with the
response in place at that point. `MBOX_STATUS` (`+0x18`) bit 31/30 are the
full/empty flags gating each side.

`mbox_property_call()` handles this generically for any tag; the
`mbox_get_*()` wrappers just fill in the one tag they need and call it. No
cache maintenance is needed around the VC's access — this kernel runs with
D-cache disabled globally (see `startup.s`).

## Clock IDs

The `MBOX_CLOCK_ID_*` list is the full set from the mailbox property-tag
spec. There is no distinct ID for I2C/BSC — `i2c_channel_init()` uses `CORE`
(id 4), matching what Linux's `i2c-bcm2835` DTB binding points its `clocks`
phandle at. That mapping is sourced, not guessed, but still unverified
against the BCM2711 datasheet directly — see `i2c/docs.md`.

## Open items

- `MBOX_SPIN_LIMIT` (1,000,000 spins) is an arbitrary timeout, not calibrated
  against real VC response latency — same caveat as the ad-hoc spin counts
  elsewhere in this codebase (e.g. UART's `uart_channel_init()` FIFO-drain
  wait). Revisit if `mbox_property_call()` ever times out on real hardware
  under load.
- Not safe to call from two cores/tasks concurrently: every `mbox_get_*()`
  wrapper shares one static request buffer (`s_mbox_buf`), and the mailbox
  hardware itself only has one transaction in flight at a time anyway. Add a
  spinlock around `mbox_property_call()` (same pattern as UART's
  `uart_buf_lock`) if a multicore caller shows up.
- `mbox_selftest()` only proves the VC responds to *a* property-tag call
  (firmware revision) — it doesn't validate any specific tag's response
  shape. Good enough for bring-up, not a substitute for checking a real
  caller's values.
