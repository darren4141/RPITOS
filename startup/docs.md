# startup

Covers `vectors.s` (shared exception vector table) and `startup.s` (reset
handler, mode/stack setup, IRQ handler, companion-core park protocol).

## Vector table: shared header, `b` not `ldr pc, =`

`vectors.s` is `.include`d first by every image's `.init` (app, bootloader,
bootstrap), so `_start` always lands at the image base and becomes the value
written to `VBAR`. The table itself is one source of truth; each includer
defines its own eight handler labels (`_reset_handler`, `_undef_handler`, …).

The eight entries are direct `b label` branches — **never** `ldr pc, =label`.
`ldr pc, =label` is a PC-relative *load*: the assembler emits `ldr pc, [pc,
#offset]` reading from a literal pool placed later in `.text`. On real CM4
hardware this read silently failed at reset (before caches/MMU are
configured, with the literal pool hundreds of bytes away) — the CPU never
branched to `_reset_handler` and the board just sat there with no visible
error. `b label` is a self-contained relative branch (±32 MB), reaches any
handler in the image with no memory access, and works immediately after
reset. See `CLAUDE.md`'s "Known Issues" for the full original bug writeup.

## DFU marker at image offset 0x20

Immediately after the 8-entry vector table (`startup.s`) sits a 4-byte magic
word, `0x44465521` (`'D' 'F' 'U' '!'`). It proves the image was built with
the standard rpitos startup, so the bootloader's `dfu_receive` will accept
it as a valid app to flash. It's reached only as data — the reset vector
branches straight over it to `_reset_handler` — and must stay in sync with
`DFU_APP_MAGIC` in `source/boot/dfu_receive/dfu_receive.h`.

## Secondary cores park immediately

The very first thing `_reset_handler` does is read `MPIDR`, extract the core
ID, and park any non-zero core (`wfe` loop, `_secondary_hang$`) before
touching anything else. Only core 0 proceeds through the rest of boot; other
cores are released later and explicitly via the companion-core protocol
below.

## HYP-mode exit

CM4/BCM2711 firmware leaves the CPU in non-secure HYP mode (0x1A). `msr
cpsr_c` cannot change the mode bits from HYP — only `eret` can, so the
sequence is: read CPSR, detect HYP, build a target CPSR (SVC mode, IRQ+FIQ
masked) into `SPSR_hyp`, point `ELR_hyp` at `_stack_setup`, then `eret`.

**`CNTHCTL_EL2` must be set here, before the `eret`.** By default
(`CNTHCTL_EL2[1:0]` = 0), non-secure EL1 access to the physical timer
registers (`CNTP_CTL`, `CNTP_CVAL`, `CNTP_TVAL`, `CNTPCT`) is disabled — the
first `mcr`/`mrrc` to any of them in `gentimer_init()` would trap as an
Undefined Instruction. Setting `PL1PCEN` (bit 1) and `PL1PCTEN` (bit 0)
enables them. This register is only writable from HYP mode, so it cannot be
deferred to SVC-mode code later.

## D-cache/I-cache disable must happen in SVC mode, not HYP mode

`mcr p15, 0, r0, c1, c0, 0` writes `SCTLR` (EL1 System Control Register) when
executed in SVC mode, but writes `HSCTLR` (the HYP System Control Register —
a completely different register) when executed in HYP mode. The cache
disable in `_stack_setup` must run *after* the HYP→SVC `eret`, or it silently
writes the wrong register and leaves the EL1 D-cache enabled by firmware.
Caches must be off before any MMIO access: without an MMU, MMIO addresses
have no Device-memory attributes, so writes without this go to cache and
never reach the peripheral.

## `_irq_handler`: context frame and dispatch

**Frame layout.** On entry, `srsdb sp!, #0x13` pushes `{lr_irq, SPSR_irq}`
onto `sp_svc` while still in IRQ mode, then `cps #0x13` switches to SVC mode
(now running on the interrupted task's own stack) and `push {r0-r12, lr}`
saves the general-purpose registers *and* `lr_svc`. `lr_svc` must be part of
the frame: GCC uses `lr` as the return address in leaf functions (`bx lr`)
and as a scratch register elsewhere, so if it weren't saved/restored per
task, the next-scheduled task would resume with the *previous* task's `lr` —
a leaf-function return would then jump into arbitrary data (e.g. another
task's stack) and trap as Undefined.

**Mailbox check before GIC IAR.** Before reading `GICC_IAR`, the handler
checks this core's ARM Local mailbox 0 (Core*N* IRQ Source register, bit 4).
The companion-core IPI (`gic_send_mailbox_ipi()`, used by
`companion_core_reset_active()`) is a separate, non-GIC signal — `GICD_IGROUPR`
is confirmed write-ignored from this Non-secure-only OS, so a GIC SGI can
never be delivered here, and this bypasses the GIC entirely (same reasoning
as `CORE_TIMER_IRQCNTL` for the timer; see `gic/docs.md`). Because it never
goes through the GIC, it would never show up via `GICC_IAR` — it has to be
checked independently or a pending IPI would be silently missed as
"spurious" and dropped.

**INTID dispatch.** GIC ID 30 (nCNTPNSIRQ, EL1 non-secure physical timer)
branches to `cntx_switch$` and stays entirely in assembly — it saves/restores
SP directly and can't go through C. Every other INTID (DMA TX = 119, UART RX
= 153, ...) goes through the C dispatch table via `irq_dispatch()`, running
on the IRQ-mode stack.

**Per-core TCB indexing.** `p_task_control_block` is one slot per core
(`TaskControlBlock *p_task_control_block[COMPANION_CORE_MAX_CORES]`);
`cntx_switch$` indexes it by `MPIDR & 3`, not a bare symbol load, and keeps
the core ID in `r5` (callee-saved per AAPCS) so it survives the two `bl`
calls to `timer_tick_handler`/`scheduler_switch_context` — `r0-r3` are
caller-saved and get clobbered by them, so `&p_task_control_block[core_id]`
is recomputed after those calls rather than reused.

## Companion-core park protocol

`mailbox_park_request$` (reached when the mailbox-0 check above fires) drains
this core's ARM Local Mailbox 0 RDCLR register — reading it clears it, no
separate write-to-clear, same as the timer's ARM-Local routing (see
`gic/docs.md`) — then branches to `_companion_core_park$`. No SP fixup is
needed at the branch: `_companion_core_park$` switches to a dedicated stack
immediately, so whatever SP holds at that point (the abandoned task's,
offset by `_irq_handler`'s own prologue) is discarded, not reused.

`_companion_core_park$` is reached only via an ARM Local mailbox 0 IPI,
triggered by `companion_core_reset_active()` on another core requesting a
DFU/software reboot. It abandons whatever task/scheduler state this core
had — the next boot's `zero_bss$` wipes it anyway (see
`companion_core_soft_reset_plan.md`'s "Race window") — and re-parks exactly
like the bootstrap's `_sec_park$`, watching the same cross-image mailbox.

**Why a dedicated park stack, not the abandoned task's own stack:**
reusing the interrupted task's stack turned out to be unsafe — its
remaining headroom depends entirely on how deep that task happened to be
when the IPI arrived (not under our control), and the fresh entry
function's own bring-up chain (`gic_percore_init`/`gentimer_init`/
`scheduler_init`/`task_create`) needs real depth of its own. Overflowing
into the abandoned stack's watermark-filled tail produced repeated
`TASK_WATERMARK`-as-return-address crashes during the investigation that
led to this fix. SP is set to `g_companion_core_park_stack` (in
`companion_core.c`) at `(core_id + 1) * 8192` bytes — 2048 words per core,
which must stay in sync with `COMPANION_CORE_PARK_STACK_WORDS` in
`companion_core.c`. There's no cross-image symbol for the bootstrap's own
per-core SVC stack to switch to instead (it lives inside the bootstrap's own
size-dependent memory layout, unlike the fixed
`CORE_MAILBOX_ADDR`/`APP_START_ADDR` region), so this park stack is
app-image-owned.

The core's own mailbox slot is zeroed immediately after computing SP —
otherwise `_companion_core_park_wait$`'s wait loop would immediately re-`blx`
the stale old entry point instead of actually waiting for a fresh release.
The wait loop (`wfe`/poll) then blocks until a non-zero value appears in
that slot and `blx`'s into it — whatever `companion_core_start()` published.

**If that entry function ever returns** (it shouldn't —
`scheduler_start()` is documented "never returns", but does have a real
`return E_EMPTY` path if the ready list is somehow empty), control goes back
to `_companion_core_park$` — **not** straight to `_companion_core_park_wait$`.
The mailbox slot still holds the same now-stale entry address (nothing
re-zeroed it after the `blx`), so branching directly to the wait loop would
immediately re-`blx` the exact same stale pointer in a tight loop instead of
waiting for a genuinely new release. This is what produced repeated `[M1]`
markers and eventual heap exhaustion the one time this path was actually
exercised — going back through `_companion_core_park$` re-zeros the slot
first, so the wait loop is guaranteed to block.

## Raw UART debug markers (`_dbg_mbox_wait*`, `_dbg_wpark_wait*`)

Both blocks are working diagnostic code, not comments — direct PL011
register pokes (bypassing `uart.c`'s ring buffer/lock entirely) that print a
literal `[M<core>]` (mailbox_park_request$) or `[W<core>]`
(_companion_core_park_wait$, right before the `blx`) so a scope/terminal
confirms a given core actually took a mailbox IPI or is about to jump into a
freshly published entry point. Kept as permanent low-level tripwires for
this exact protocol, since it has no GIC-visible interrupt to trace through
normal means.

## `start_first_task`

Called once per core, from that core's own `scheduler_start()`. Indexes
`p_task_control_block` by `MPIDR & 3` the same way `cntx_switch$` does, loads
that core's first TCB's saved SP, then pops the initial `{r0-r12, lr, pc,
spsr}` frame and `rfeia`s into it — the same frame layout a normal task
context switch expects, just seeded rather than saved from a real interrupt.
