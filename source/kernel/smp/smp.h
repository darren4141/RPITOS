#ifndef SMP_H
#define SMP_H

#include <stdint.h>

#include "status.h"

#define SMP_MAX_CORES 4

/**
 * @brief Assign an entry function to a secondary core (1..3) and release it from the bootstrap parking loop.
 * @note `entry` runs forever on that core and must never return, must not call
 * blocking kernel APIs (the core has no TCB and is not scheduled), and must
 * not touch data shared with another core without a spinlock — enter_critical
 * only masks interrupts on the calling core.
 * @return E_INVALID_ARGS for core 0 or an id >= SMP_MAX_CORES.
 */
StatusCode smp_start_core(uint32_t core_id, void (*entry)(void));

/**
 * @brief Return this core's id (0..3), read from MPIDR.
 */
uint32_t smp_core_id(void);

#endif
