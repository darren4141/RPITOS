# delay

Two unrelated delay mechanisms, not a graduated API:

- `delay_cycles()` is a pure busy-wait NOP loop — no dependency on the
  scheduler, safe to call before `scheduler_init()`.
- `delay_ms()` busy-waits on the shared scheduler tick counter (linked via
  `delay_init()`). Despite the name it counts *ticks*, not wall-clock
  milliseconds — the mapping depends on the scheduler's configured `hz`.
  It still busy-waits rather than blocking the calling task, so it's not a
  replacement for `task_delay_ms()` in `scheduler.h`.
