# software_timer

Software timers on top of the scheduler tick. A timer node lives in at most
one of two intrusive singly-linked lists at a time (`TimerListId`):
`TIMER_LIST_BLOCKED` (armed, sorted by ascending `expiry_tick` so the tick
handler can stop scanning at the first non-expired timer) or
`TIMER_LIST_ACTIVE` (expired, waiting for the service task). A single
`next` pointer plus the list-id tag replace the generic `List`/`ListItem`
machinery used elsewhere, since a timer never needs prev/tail pointers or
to belong to more than one list.

`software_timer_tick()` runs in IRQ context with interrupts already masked
and only moves expired timers to the active list and signals a semaphore —
the actual callback runs later, in the dedicated service task
(`software_timer_service_task`), so a slow callback can't extend the tick
ISR. Periodic timers are re-armed relative to their *scheduled* expiry
(`expiry_tick += period`), not the time the callback actually ran, so they
don't drift under load.
