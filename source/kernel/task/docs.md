# task

`task_create()` allocates from two static pools: a fixed-size `TaskControlBlock`
array (`tcb_pool`, `MAX_NUM_TASKS` entries — never freed, never reused even
if a task could exit) and a per-task stack from the kernel heap
(`heap_malloc`, see `heap/docs.md`). TCBs are deliberately never adjacent to
any task's stack in memory, so a stack overflow can corrupt at most that
task's own state, never another task's TCB.

`task_init_stack()` hand-builds the initial exception-return frame
(`[R0-R12][LR][PC][SPSR]`) so that `start_first_task()`/the context-switch
path in `startup.s` can `pop`+`rfeia` a never-yet-run task exactly the same
way it resumes a previously-preempted one — see `scheduler/docs.md`.
