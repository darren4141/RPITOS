#ifndef COMPANION_CORE_H
#define COMPANION_CORE_H

#include <stdint.h>

#include "status.h"

#define COMPANION_CORE_MAX_CORES 4

/**
 * @brief Shared companion-core bookkeeping; see docs.md's "Multicore watchdog" section.
 * @note Pointer-owned by the caller — see docs.md for storage-duration rules.
 */
typedef struct {
  volatile uint32_t expected_mask;
  volatile uint8_t core_kicked[COMPANION_CORE_MAX_CORES];
} CompanionCoreContext;

/**
 * @brief Link `context` to the companion_core module; marks core 0 expected.
 */
StatusCode companion_core_init(CompanionCoreContext *context);

/**
 * @brief Assign an entry function to a secondary core (1..3) and release it from the bootstrap parking loop.
 * @note `entry` runs forever on that core and must never return, must not call
 * blocking kernel APIs (the core has no TCB and is not scheduled), and must
 * not touch data shared with another core without a spinlock — enter_critical
 * only masks interrupts on the calling core.
 * @note Also marks core_id expected in the linked CompanionCoreContext, if any — see docs.md.
 * @return E_INVALID_ARGS for core 0 or an id >= COMPANION_CORE_MAX_CORES.
 */
StatusCode companion_core_start(uint32_t core_id, void (*entry)(void));

/**
 * @brief Return this core's id (0..3), read from MPIDR.
 */
uint32_t companion_core_id(void);

/**
 * @brief Force every companion core released since the last call back into the bootstrap-style mailbox park loop.
 * @note For use before a DFU/software reboot (see reset.c's enter_bootloader()),
 * so a core that's still running its old entry function doesn't miss the next
 * companion_core_start() call after reboot and require a physical power cycle.
 * Sends each alive core an ARM Local mailbox 0 IPI (gic_send_mailbox_ipi()) —
 * fire-and-forget, does not wait for confirmation the core actually parked.
 * See companion_core/docs.md's "Race window" note for why that's safe here.
 */
void companion_core_reset_active(void);

#endif
