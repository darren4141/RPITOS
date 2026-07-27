# dfu_receive

Blocking DFU packet receiver and image-write state machine, run by the
bootloader after it detects a DFU trigger (see `dfu_trigger`).

## Wire protocol

A packet is `[0xAA SOF][CMD][LEN hi][LEN lo][DATA...][CRC 4 bytes][0xBB EOF]`,
parsed byte-at-a-time by `dfu_packet_receive()`'s state machine
(`PacketState`). Commands (`Commands` in `dfu_receive.h`):

| Command | Purpose |
|---|---|
| `CMD_REQ` | version/slot info request (not yet implemented) |
| `CMD_START` | begin an app image transfer into the inactive A/B slot |
| `CMD_START_SELF_UPDATE` | begin a bootloader self-update transfer |
| `CMD_DATA` | a chunk of image data, written to eMMC as sectors fill |
| `CMD_FINISH` | CRC-check, validate, and commit the transfer |
| `CMD_ABORT` | abandon the session |
| `CMD_GET_STATUS` / `CMD_RECOVER` | not yet implemented |

## Commit sequence (`CMD_FINISH`, app image)

1. Compare the in-flight CRC against the header's expected CRC.
2. Flush any partially-filled sector still buffered in RAM.
3. Read the just-written slot back from eMMC and validate it (`boot_validate_app`)
   — the in-flight CRC only proves the UART transfer was intact, not that the
   bytes actually landed correctly on eMMC.
4. Verify the image carries the `DFU_APP_MAGIC` marker at
   `DFU_APP_MARKER_OFFSET`, proving it was linked with the standard rpitos
   startup (and therefore has DFU-trigger support) — read back from eMMC,
   not trusted from the stream.
5. Flip `wdt_meta.active_app_slot` to the new slot, mark it on-trial
   (`app_slot_trial = 1`), and persist.

At every failure point above the *active* slot is untouched — only the
inactive slot was ever written, so an aborted or failed DFU session can't
brick a working app.
