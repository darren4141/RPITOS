# Makefile

## Header-dependency tracking (`-MMD -MP`)

`CFLAGS` includes `-MMD -MP` so each object gets a `.d` file listing the
headers it `#include`s; `-include $(SAMPLE_OBJECTS:.o=.d)` pulls those back
into `make`'s dependency graph. Without this, the compile rules only depend
on the `.c` file, so a header-only change (no `.c` touched) does not trigger
a rebuild and a stale `.o` can silently keep old header values (enum sizes,
struct layouts, ...) baked in indefinitely.

This is exactly what happened to `telemetry_frame.o`: `telemetry.h` grew
`PKT_SYNC_CREATED`/`PKT_MUTEX_OWNER_CHANGED` (`NUM_TELEMETRY_PACKET_TYPES`
9 → 11) two days after `telemetry_frame.c` was last touched, so `make` never
rebuilt it and its bounds check (`type >= NUM_TELEMETRY_PACKET_TYPES`) kept
silently dropping both new packet types — no compile error, no warning, just
bytes that never reached the wire. See `md/client/device/sync_view.md`'s
postmortem.
