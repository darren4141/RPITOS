# spinlock

Cross-core mutual-exclusion primitive the rest of the kernel is missing:
`enter_critical()`/`exit_critical()` only mask IRQs on the calling core, so
they do nothing against a second core touching the same memory at the same
time.

## How it works — Lamport's Bakery algorithm, not LDREX/STREX

`Spinlock` is Lamport's Bakery lock: each core "draws a ticket" (one higher
than the max of every other core's current ticket, published via a
`choosing[]` flag so nobody reads a half-written ticket), then waits for
every other competing core whose `(ticket, core_id)` sorts before its own.
Pure volatile loads/stores plus `dmb`/`dsb` barriers — **no atomic hardware
instructions at all.**

That's deliberate, not a stylistic choice: an earlier LDREX/STREX-based
ticket lock was tried first and **hung forever on real hardware** at the very
first cross-core acquire (confirmed via UART checkpoints — it printed
"before scheduler_lock" and then never came back; the watchdog eventually
force-reset the board). Root cause: this system runs with the MMU disabled,
so all memory — including a `Spinlock` — defaults to Device-nGnRnE, not
Normal. LDREX/STREX are architecturally deprecated against Device memory,
and specifically the *global* exclusive monitor that lets one core's STREX
correctly detect "did another core touch this address" depends on the
memory being marked Shareable, an attribute normally only configured via
MMU page-table attributes. Without the MMU, STREX apparently never
succeeded — QEMU's more permissive LDREX/STREX emulation didn't reproduce
this, which is why it only showed up once flashed to the actual board.
The Bakery algorithm sidesteps the whole problem: it needs no exclusive
monitor, just memory accesses that become visible to other cores in program
order — which is exactly what Device-nGnRnE's strongly-ordered semantics
already guarantee.

`choosing[]`/`ticket[]` are `uint32_t`, not a narrower type, to stay
word-aligned/word-sized — Device-nGnRnE memory requires that (see
`boot_chain.md`'s `StartPacket` comment for the same rule elsewhere in this
codebase).

## Why cross-core exclusion is needed on this SoC at all

BCM2711's 4 Cortex-A72 cores share one physical RAM over a common
interconnect. D-cache is off on every core (see the top-level `CLAUDE.md`
startup notes) — deliberately: with no cache, every load/store from every
core goes straight to RAM, so there's no cache-coherency protocol to worry
about and no cache-maintenance instructions needed for cross-core
visibility. That does *not* make shared data safe on its own, though: a
struct/list mutation is usually several sequential stores, and two cores
interleaving their individual stores into the same multi-step sequence
corrupts it even though each store itself is atomic — that's what this lock
actually prevents.

## Where it's used

`scheduler.c` gives **each core its own** `Spinlock` (`scheduler_lock(core_id)`/
`scheduler_unlock(core_id)` in `scheduler.h` — not one global lock, so
routine ticking on core N never contends core M's) guarding that core's
ready/blocked lists. Since each core runs its own independent scheduler
instance (see `scheduler/docs.md`), a `TaskControlBlock` carries the
`core_id` of the scheduler it belongs to — callers lock the *target* task's
core, not necessarily their own, which is what makes it safe for
`semaphore_give()`/`mutex_unlock()` called on one core to correctly move a
task that lives on a different core's ready list. `Semaphore`/`Mutex` each
carry their own separate `Spinlock` too (they aren't owned by any one core —
their waiters can belong to different cores). `heap.c` owns one guarding the
bump allocator against concurrent `heap_malloc()` calls from different
cores' `task_create()`. `uart.c` owns one (`uart_buf_lock`) guarding the
ring-buffer reserve-a-slot-and-write step in `uart_tx()`, so
`uart_send_byte()`/`uart_print()`/`uart_printf()` are safe to call from any
core, not just the one that ran `uart_task_start()`.

Not reentrant — do not acquire a `Spinlock` from a context that might already
hold it on the same core. Lock order matters when more than one is held at
once: `enter_critical()` → a `Semaphore`/`Mutex`'s own lock → a core's
`scheduler_lock()`, always in that order, never reversed — see
`scheduler/docs.md`'s "Locking model".

## Why pair with `enter_critical()`

`spinlock_acquire()` alone is not enough to guard a critical section against
same-core preemption: the Bakery lock's `ticket[]` slot is per-*core*, not
per-task. Without also masking IRQs (`enter_critical()`), a timer tick can
preempt the holding task mid-critical-section and switch to a different task
on the *same* core that also calls into the same spinlock — that second
call's `spinlock_acquire()` overwrites the first task's still-in-use ticket
slot for that core and proceeds concurrently with it. Every caller in this
codebase (`heap.c`, `uart.c`, `mutex.c`, `semaphore.c`) wraps
`spinlock_acquire()`/`spinlock_release()` in `enter_critical()`/`exit_critical()`
for this reason.
