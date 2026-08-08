#ifndef SEMAPHORE_H
#define SEMAPHORE_H

#include "spinlock.h"
#include "status.h"
#include "task_types.h"

typedef struct Semaphore Semaphore;

#define SEMAPHORE_TAKE_BLOCKING       -1
#define SEMAPHORE_TAKE_NO_TIMEOUT     0U
#define SEMAPHORE_MAX_COUNT_UNLIMITED 0xFFFFFFFFU

struct Semaphore {
  uint32_t max_count;
  uint32_t count;
  List semaphore_blocked_list;
  // A semaphore isn't owned by any one core (its waiters can belong to
  // different cores' schedulers), so it carries its own lock rather than
  // relying on any core's scheduler lock. See spinlock/docs.md.
  Spinlock lock;
#ifdef RTOS_TELEMETRY
  // See Mutex::sync_id (mutex.h) — same pattern, field absent entirely outside
  // telemetry builds. Also used for a queue's two internal semaphores
  // (queue_init()), each with their own sync_id and a parent_sync_id back to
  // the queue — see md/client/device/sync_view.md.
  uint16_t sync_id;
#endif
};

/**
 * @brief Initialize a counting semaphore with the given max and starting count.
 * @param name Only used (broadcast once, never stored) when built with RTOS_TELEMETRY; pass NULL or a literal freely either way.
 */
void semaphore_init(Semaphore *smph, uint32_t max_count, uint32_t initial_count, const char *name);

#ifdef RTOS_TELEMETRY
/**
 * @brief Same as semaphore_init(), plus a parent_sync_id link. Queue-internal use only (queue_init()) — every other caller wants the public semaphore_init() above.
 */
void semaphore_init_with_parent(Semaphore *smph, uint32_t max_count, uint32_t initial_count, const char *name,
                                 uint16_t parent_sync_id);
#endif

/**
 * @brief Take one count, blocking up to timeout_ms if none are available.
 * @note Safe to call from a task on any core.
 */
StatusCode semaphore_take(Semaphore *smph, int64_t timeout_ms);

/**
 * @brief Give one count back, waking the longest-waiting blocked task if any.
 * @note Safe to call from any core, including one with no scheduler of its
 * own — the woken task is correctly placed back on its own core's ready
 * list regardless of which core called this.
 */
StatusCode semaphore_give(Semaphore *smph);

#endif
