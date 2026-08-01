#ifndef SPINLOCK_H
#define SPINLOCK_H

#include <stdint.h>

#include "companion_core.h"

/**
 * @brief Lamport's Bakery lock for mutual exclusion across cores.
 * @note Uses no atomic hardware instructions (no LDREX/STREX) — this system
 * runs with the MMU disabled, so all memory (including this struct) is
 * Device-nGnRnE, not Normal. LDREX/STREX are architecturally deprecated
 * against Device memory, and the *global* exclusive monitor that lets one
 * core's STREX correctly detect "did another core touch this address"
 * depends on the memory being marked Shareable — an attribute normally only
 * configured via MMU page-table attributes. Confirmed on real hardware: a
 * plain LDREX/STREX ticket lock hung forever (STREX never succeeded) at the
 * very first cross-core acquire. The Bakery algorithm needs only plain
 * volatile loads/stores plus memory barriers, which is exactly what
 * Device-nGnRnE's strongly-ordered semantics already provide.
 */
typedef struct {
  // uint32_t, not a narrower type — Device-nGnRnE memory (the default with
  // the MMU off) requires word-aligned, word-sized accesses; see
  // boot_chain.md's StartPacket comment for the same rule elsewhere in this
  // codebase.
  volatile uint32_t choosing[COMPANION_CORE_MAX_CORES];
  volatile uint32_t ticket[COMPANION_CORE_MAX_CORES];
} Spinlock;

/**
 * @brief Initialize a spinlock to the unlocked state.
 */
void spinlock_init(Spinlock *lock);

/**
 * @brief Acquire the lock, spinning until it's this core's turn.
 * @note Reads companion_core_id() to identify the caller — do not call this
 * recursively on the same lock from the same core, it is not reentrant.
 */
void spinlock_acquire(Spinlock *lock);

/**
 * @brief Release the lock.
 */
void spinlock_release(Spinlock *lock);

#endif
