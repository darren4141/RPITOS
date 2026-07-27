# task_types

Shared type definitions for the scheduler, task, mutex, semaphore, and
software_timer components — `TaskControlBlock`, the intrusive `List`/`ListItem`
types used by every blocked/ready list in the kernel, and the task
priority/state enums. Header-only; no `.c` file.

See `task/docs.md` for how a `TaskControlBlock` gets allocated and
initialized, and `scheduler/docs.md` for how its list fields get used.
