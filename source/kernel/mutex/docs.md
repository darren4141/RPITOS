# mutex

Blocking mutex with optional priority inheritance, following the FreeRTOS
approach: a task's priority is only ever restored on unlock once it holds
zero inheritance-enabled mutexes (`mutexes_held == 0`), so nested locks
don't prematurely demote a boosted task. See the `mutex_inheritance` sample
for a worked example and expected trace.

`mutex_unlock()`'s handoff path is O(n) in the number of waiters: it scans
the blocked list once to find both the highest-priority waiter (the next
owner) and the second-highest (the new `inherited_priority` for the
mutex), rather than doing two passes.
