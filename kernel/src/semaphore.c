#include "semaphore.h"

#include <stddef.h>

#include "interrupts.h"
#include "scheduler.h"

static void semaphore_add_to_blocked_list(Semaphore *smph, TaskControlBlock *tcb)
{
  ListItem *ei = &tcb->event_list_item;
  ei->owner = tcb;
  ei->container = &smph->semaphore_blocked_list;
  ei->next = NULL;
  ei->prev = smph->semaphore_blocked_list.list_end;

  if (smph->semaphore_blocked_list.list_end != NULL) {
    smph->semaphore_blocked_list.list_end->next = ei;
  }
  else {
    smph->semaphore_blocked_list.head = ei;
  }
  smph->semaphore_blocked_list.list_end = ei;
  smph->semaphore_blocked_list.num_items++;
}

void semaphore_init(Semaphore *smph, uint32_t max_count, uint32_t initial_count)
{
  smph->max_count = max_count;
  smph->count = initial_count;

  smph->semaphore_blocked_list.head = NULL;
  smph->semaphore_blocked_list.index = NULL;
  smph->semaphore_blocked_list.list_end = NULL;
  smph->semaphore_blocked_list.num_items = 0;
}

StatusCode semaphore_take(Semaphore *smph, int64_t timeout_ms)
{
  uint64_t deadline = (timeout_ms > 0) ? scheduler_get_tick_count() + (uint64_t)timeout_ms : 0;
  TaskControlBlock *cur_tcb = scheduler_get_current_task();

  while (1) {
    uint32_t cpsr = enter_critical();

    if (smph->count > 0) {
      smph->count--;
      exit_critical(cpsr);
      return E_OK;
    }

    if (timeout_ms == 0) {
      exit_critical(cpsr);
      return E_TIMED_OUT;
    }

    if ((timeout_ms > 0) && (scheduler_get_tick_count() >= deadline)) {
      exit_critical(cpsr);
      return E_TIMED_OUT;
    }

    semaphore_add_to_blocked_list(smph, cur_tcb);
    removeFromReadyList(&cur_tcb);

    if (timeout_ms > 0) {
      scheduler_add_to_blocked_list(cur_tcb, deadline);
    }

    cur_tcb->currentState = TASK_STATE_BLOCKED;
    exit_critical(cpsr);

    while (cur_tcb->currentState == TASK_STATE_BLOCKED) {}
  }
}

StatusCode semaphore_give(Semaphore *smph)
{
  uint32_t cpsr = enter_critical();

  if (smph->count < smph->max_count) {
    smph->count++;
  }
  else {
    exit_critical(cpsr);
    return E_RESOURCE_EXHAUSTED;
  }

  if (smph->semaphore_blocked_list.num_items > 0) {
    if (smph->semaphore_blocked_list.num_items == 1) {
      smph->semaphore_blocked_list.list_end = NULL;
    }

    TaskControlBlock *p_next_tcb = smph->semaphore_blocked_list.head->owner;
    smph->semaphore_blocked_list.head = smph->semaphore_blocked_list.head->next;
    if (smph->semaphore_blocked_list.head != NULL) {
      smph->semaphore_blocked_list.head->prev = NULL;
    }
    smph->semaphore_blocked_list.num_items--;


    scheduler_remove_from_blocked_list(p_next_tcb);
    p_next_tcb->event_list_item.next = NULL;
    p_next_tcb->event_list_item.prev = NULL;
    p_next_tcb->event_list_item.container = NULL;
    addToReadyList(&p_next_tcb);
  }

  exit_critical(cpsr);
}