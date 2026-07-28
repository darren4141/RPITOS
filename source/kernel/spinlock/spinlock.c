#include "spinlock.h"

void spinlock_init(Spinlock *lock)
{
  for (uint32_t i = 0; i < SMP_MAX_CORES; i++) {
    lock->choosing[i] = 0;
    lock->ticket[i] = 0;
  }
}

void spinlock_acquire(Spinlock *lock)
{
  uint32_t me = smp_core_id();

  // Announce "drawing a ticket" before computing it, so another core reading
  // our ticket mid-computation knows to wait rather than race ahead — the
  // classic Bakery two-step publish.
  lock->choosing[me] = 1;
  __asm__ volatile ("dmb sy" ::: "memory");

  uint32_t max_ticket = 0;
  for (uint32_t i = 0; i < SMP_MAX_CORES; i++) {
    uint32_t t = lock->ticket[i];
    if (t > max_ticket) {
      max_ticket = t;
    }
  }
  lock->ticket[me] = max_ticket + 1;
  __asm__ volatile ("dmb sy" ::: "memory");
  lock->choosing[me] = 0;
  __asm__ volatile ("dmb sy" ::: "memory");

  // Wait for every core that either has a smaller ticket, or tied with a
  // smaller core id (the standard Bakery tie-break) — lexicographic order
  // on (ticket, core_id) decides who goes first.
  for (uint32_t i = 0; i < SMP_MAX_CORES; i++) {
    if (i == me) {
      continue;
    }

    while (lock->choosing[i]) {
      __asm__ volatile ("dmb sy" ::: "memory");
    }

    while (1) {
      uint32_t their_ticket = lock->ticket[i];
      __asm__ volatile ("dmb sy" ::: "memory");

      if (their_ticket == 0) {
        break;   // core i isn't competing for the lock
      }
      if (their_ticket > lock->ticket[me]) {
        break;   // our ticket is smaller — we go first
      }
      if ((their_ticket == lock->ticket[me]) && (i > me)) {
        break;   // tie — smaller core id goes first, and that's us
      }
      // otherwise core i has priority — keep waiting on it
    }
  }

  __asm__ volatile ("dmb sy" ::: "memory");
}

void spinlock_release(Spinlock *lock)
{
  uint32_t me = smp_core_id();

  __asm__ volatile ("dmb sy" ::: "memory");   // critical-section writes visible before we step out of line
  lock->ticket[me] = 0;
  __asm__ volatile ("dsb sy" ::: "memory");   // visible to other cores before this function returns
}
