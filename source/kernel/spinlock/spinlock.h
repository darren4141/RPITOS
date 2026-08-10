#ifndef SPINLOCK_H
#define SPINLOCK_H

#include <stdint.h>

#include "companion_core.h"

/**
 * @brief Lamport's Bakery lock for mutual exclusion across cores.
 * @note No atomic instructions (LDREX/STREX) — this runs with the MMU off,
 * and a plain LDREX/STREX ticket lock hung forever on real hardware in that
 * config. See spinlock/docs.md for why.
 */
typedef struct {
  // uint32_t, not narrower — Device-nGnRnE (the default with the MMU off)
  // requires word-aligned, word-sized accesses. See spinlock/docs.md.
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
