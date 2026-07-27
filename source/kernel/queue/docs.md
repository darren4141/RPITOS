# queue

Fixed-capacity byte-copy message queue, built on top of two `semaphore`s
(`space_available`, `data_available`) rather than its own wait list —
`queue_send`/`queue_recv` block by taking a semaphore, so all the
list-management and priority-wakeup logic lives in one place (`semaphore.c`)
instead of being duplicated here.

The backing buffer is allocated once, from the kernel heap, in
`queue_init()` — see `heap/docs.md`.
