#ifndef SOFTWARE_TIMER_H
#define SOFTWARE_TIMER_H

#include "stdbool.h"
#include "stdint.h"

#include "status.h"
#include "task_types.h"

// Callback receives a pointer to the SoftwareTimer that fired.
typedef void (*TimerCallback)(void *arg);

typedef enum {
  TIMER_MODE_ONE_SHOT,
  TIMER_MODE_PERIODIC,
} TimerMode;

// Which internal list a timer currently lives in. A timer node belongs to at
// most one list at a time, so a single `next` pointer plus this tag replace the
// generic List/ListItem machinery (those are owned by TaskControlBlocks). No
// prev/tail pointers are needed.
typedef enum {
  TIMER_LIST_NONE,     // unarmed: created before the scheduler started, stopped, or a fired one-shot
  TIMER_LIST_BLOCKED,  // armed, counting down to expiry_tick
  TIMER_LIST_ACTIVE,   // expired, waiting for the service task to run its callback
} TimerListId;

typedef struct SoftwareTimer SoftwareTimer;

struct SoftwareTimer {
  uint64_t expiry_tick;
  uint64_t period;

  TimerMode timer_mode;
  TimerCallback callback;

  TimerListId list_id;   // which list this node is currently in
  SoftwareTimer *next;   // intrusive singly-linked list link
};

StatusCode software_timer_init();
StatusCode software_timer_start();

StatusCode software_timer_create(SoftwareTimer *software_timer, uint64_t period, TimerCallback callback_function, TimerMode timer_mode);
StatusCode software_timer_stop(SoftwareTimer *software_timer);
StatusCode software_timer_reset(SoftwareTimer *software_timer);

// Called from the scheduler tick (IRQ context, interrupts already masked).
// Moves every timer whose expiry_tick has passed from the blocked list to the
// active list and signals the service task once per expired timer.
void software_timer_tick(uint64_t now_tick);

#endif
