# semaphore

Counting semaphore: blocking `take`/`give` backed by a FIFO blocked list.
`SEMAPHORE_MAX_COUNT_UNLIMITED` opts out of the max-count ceiling for
semaphores used purely as a signal (e.g. the software-timer service task's
wake signal), where `give()` should never fail with `E_RESOURCE_EXHAUSTED`.

Shares its wait-list machinery (`ListItem`/`List`) with `mutex` and the
scheduler's blocked list — see `task_types.h`.
