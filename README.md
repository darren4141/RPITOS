# rpitos

A bare-metal RTOS for the Raspberry Pi CM4, written in C and ARM assembly — plus a
matching bootloader/DFU update chain, an eMMC/watchdog-backed A/B slot system, and a
Rust telemetry GUI for watching it all run live.

## Features

**Kernel**
- Preemptive, priority-based scheduler (6 priority levels) with round-robin at equal priority
- IRQ-driven context switching — full AArch32 register save/restore (r0–r12, LR, SPSR) on each task's own SVC stack
- ARM Generic Timer (CNTP) tick source at 1 kHz
- Kernel objects: mutex (priority inheritance), counting semaphore, fixed-capacity queue, software timers (own service task, drift-free re-arm)
- Static bump-allocator heap, no `free()` — no dynamic allocation surprises in the kernel
- Intrusive doubly-linked `List` / `ListItem` for O(1) ready-list operations

**Multicore (AMP)**
- Each core (0–3) runs its own independent scheduler instance — tasks never migrate cores
- `companion_core` releases/re-parks secondary cores via a fixed-address mailbox handoff
- Cross-core mutual exclusion via a Lamport's Bakery spinlock 
- Mutex/semaphore/queue hand off across cores; multicore-mode watchdog AND-gates the kick across all live cores

**Bootloader / update chain**
- Three-stage boot: `bootstrap` (0x8000, loaded by GPU firmware) → `bootloader` (0x10000) → app (0x88400)
- DFU-over-UART protocol (`dfu_receive`/`dfu_trigger`) with CRC32-checked packets and a 5-step commit (validate → flush → read-back → magic-marker → slot flip)
- A/B app and bootloader slots on eMMC with trial-boot confirm/rollback, driven by the watchdog
- `wiper` recovery image forces a bricked board back into DFU mode

**Drivers**
- GPIO, GIC-400, ARM Generic Timer, PL011 UART (buffered+IRQ or blocking, optional DMA TX), legacy DMA (normal + DMA Lite channels), eMMC, CRC32 (ARMv8 hardware instruction), PM watchdog (A/B trial boot + multicore gate), JTAG enable

**Telemetry**
- Compile-time-gated (`RTOS_TELEMETRY`) UART streaming of scheduler activity — task lifecycle, block/unblock, sync-object ownership, boot/DFU milestones — to a live host dashboard
- Companion Rust GUI (`client/`, submodule) built on `egui`, decodes the same wire framing and renders scheduler dynamics, core status, and DFU controls in real time

## Target Hardware

Raspberry Pi CM4 (BCM2711, Cortex-A72) running in AArch32 mode. Boot image: `kernel7l.img`.
`config.txt` must set `arm_64bit=0`.

## Build

```bash
make                          # build every sample in samples.json (3 boot images + 7 RTOS samples)
make SAMPLE=rtos/full_demo    # build one sample -> build/rtos/full_demo/{*.elf,.img,.hex,.list,.map}
make SAMPLE=boot/bootloader   # build the bootloader image
make clean                    # rm -rf build/
```

Toolchain: `arm-none-eabi-{gcc,as,ld,objcopy,objdump}`. Each sample declares its own
component list (drivers/kernel/boot/libs it links, telemetry on/off) in a `config.mk`
next to its `main.c` — this is how, e.g., the bootloader excludes the RTOS kernel
entirely, or `multicore_full_demo` opts into telemetry.

## Tasks

Tasks are the basic unit of execution. Each task runs a C function with its own stack and is assigned a static priority (0–5, where 5 is highest).

```c
void my_task(void *params) {
    uint64_t last_wake = tick_count;
    while (1) {
        // do work
        task_delay_until_ms(&last_wake, 100);  // run every 100 ms, no drift
    }
}

// In kmain, before starting the scheduler:
TaskControlBlock *tcb = NULL;
task_create(my_task, 512, TASK_PRIORITY_3, NULL, &tcb);
```

**Scheduler behaviour**

- On each 1 ms tick the scheduler picks the highest-priority non-empty ready list.
- Tasks at the same priority share the CPU in round-robin order, one quantum per tick.
- A blocked task (in `task_delay_ms` / `task_delay_until_ms`) is held in a delay list sorted by wakeup time and moved back to the ready list by the tick handler when its time expires.
- On multicore builds, each core runs this independently — a task created on core 1 stays on core 1.

## Project Structure

```
startup/            shared vector table + reset/IRQ handler, .include'd by every image
source/
├── kernel/          scheduler, task, mutex, semaphore, queue, software_timer,
│                     heap, spinlock, companion_core, task_types
├── drivers/          gpio, gic, gentimer, uart, dma, emmc, crc, watchdog, reset,
│                     interrupts, jtag
├── boot/             boot, boot_flags, dfu_receive, dfu_trigger, memory_map
│                     (bootstrap/bootloader/app address contract + DFU state machine)
├── telemetry/        UART telemetry framer + app-side / boot-side senders
├── libraries/        tick-based delay, status helpers
└── samples/
    ├── boot/          bootstrap, bootloader, wiper  (no RTOS)
    └── rtos/          full_demo, heavy_overlap, preemption, mutex_inheritance,
                        software_timer, multicore_blink, multicore_full_demo
client/              git submodule — Rust workspace: telemetry-protocol (decode) +
                     telemetry-gui (egui desktop app), reads the live telemetry UART
scripts/             dfu_flash.py (DFU flash + serial terminal), hex_dump.py
tools/               OpenOCD configs (cm4-jtag.cfg, ft2232h.cfg), make_fw_image.py
```

Each subsystem directory under `source/` has its own `docs.md`, and each sample under
`source/samples/` has its own `README.md`, with more detail than fits here.

## Samples

| Sample | Demonstrates |
|---|---|
| `boot/bootstrap` | Stage 1 — CRC-validates the bootloader from eMMC, jumps to it |
| `boot/bootloader` | Stage 2 — validates + jumps to the active app slot; falls into DFU mode on failure/trigger |
| `boot/wiper` | Recovery image: wipes slot headers + boot flags, forces DFU mode |
| `rtos/full_demo` | Queue producer/consumer, semaphore giver/taker, stack-watermark monitor, DFU trigger |
| `rtos/heavy_overlap` | Scheduled workload on core 0 concurrent with an unmanaged bare-metal loop on core 1 |
| `rtos/preemption` | Baseline: 3 tasks, different priorities, no mutex |
| `rtos/mutex_inheritance` | Priority inheritance across a shared mutex (LOW/MID/HIGH tasks) |
| `rtos/software_timer` | 4 periodic timers + 1 one-shot |
| `rtos/multicore_blink` | Core 0 runs the RTOS and releases core 1, which boots its own independent scheduler |
| `rtos/multicore_full_demo` | Mutex/semaphore/queue proven across cores 0–2; core 3 dedicated to telemetry |

## Status

Done:
- Scheduler, kernel objects (mutex/semaphore/queue/software timer), static heap
- GPIO/GIC/timer/UART/DMA/eMMC/CRC/watchdog/JTAG drivers
- Three-stage boot chain with UART DFU updates and A/B trial-boot rollback
- AMP multicore support with cross-core sync objects and a multicore watchdog gate
- UART telemetry streaming + Rust `egui` client GUI (device side built; GUI not yet validated against real hardware end-to-end)

To do:
- More hardware drivers (I2S, PWM, I2C)
- Raspberry Pi PMU (performance monitoring unit) integration
- Broader real-hardware validation of the telemetry client GUI
