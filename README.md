# rpitos

A bare-metal RTOS for the Raspberry Pi CM4, written in C and ARM assembly.

## Features

- Preemptive, priority-based scheduler (6 priority levels) with round-robin at equal priority
- IRQ-driven context switching — full AArch32 register save/restore (r0–r12, LR, SPSR) on each task's own SVC stack
- ARM Generic Timer (CNTP) tick source at 1 kHz
- GIC-400 interrupt controller driver
- PL011 UART driver with ring-buffer TX and a dedicated UART task
- Intrusive doubly-linked `List` / `ListItem` for O(1) ready-list operations
- Static memory only, no heap allocation in the kernel

## Target Hardware

Raspberry Pi CM4 (BCM2711, Cortex-A72) running in AArch32 mode. Boot image: `kernel7l.img`.

## Build

```bash
make        # produces kernel7l.img and build/output.elf
make clean
make qemu   # run under QEMU raspi2b (validation)
```

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

## Project Structure

```
startup/    vector table, reset handler, mode switches, stack setup
drivers/    GPIO, GIC-400, ARM Generic Timer, PL011 UART
kernel/     scheduler, task creation, List/ListItem primitives
libraries/  tick-based delay
source/     kmain and task definitions
```

## Status

To do:
- Kernel objects (mutex, semaphore)
- Software timers
- Implement more hardware drivers (I2S, PWM, I2C)
- Integrate Raspberry Pi PMU (performance monitoring unit)
- Bootloader & Watchdog
- Multi-core support
