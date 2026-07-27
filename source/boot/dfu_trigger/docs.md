# dfu_trigger

Trivial 4-byte shift-register matcher for the DFU trigger key
(`DF 00 DF 00`), shared by both the bootloader's blocking recovery-window
read and the running app's UART RX interrupt path.

## Why the app reboot goes through a task, not the ISR

`dfu_trigger_feed_isr()` runs from `uart_rx_irq_handler()` for every RX byte,
on every app, unconditionally — this is what makes DFU recovery work even
for an app that never touches the DFU APIs itself. On a key match it only
signals a semaphore; it does not call `enter_bootloader()` directly, because
that zeros the banked stacks — including the live IRQ stack it would be
running on. `dfu_trigger_task_start()` (called unconditionally from
`scheduler_init()`) creates the task that actually performs the reboot, in
task context, on its own stack.
