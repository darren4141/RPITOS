#include "task.h"
#include "uart.h"

#include <stddef.h>

static uint16_t task_counter = 0;

static TaskControlBlock tcb_pool[MAX_NUM_TASKS];

static void task_exit_trap(void);

static StackType_t *task_init_stack(StackType_t *top_of_stack, TaskFunction task_function, void *task_params)
{
  // SPSR — SVC mode, interrupts enabled, Thumb or ARM
  *top_of_stack = 0x00000013;      // SVC mode, ARM state, IRQs enabled
  top_of_stack--;

  // PC — task entry point
  *top_of_stack = (StackType_t)task_function;
  top_of_stack--;

  // LR — must match the [r0-r12][lr][pc][spsr] frame that _irq_handler saves
  // with push {r0-r12, lr}. Points at the exit trap so a task function that
  // returns is caught instead of branching to garbage.
  *top_of_stack = (StackType_t)task_exit_trap;
  top_of_stack--;

  // R12 down to R1 — all zero
  for (int i = 12; i >= 1; i--) {
    *top_of_stack = 0;
    top_of_stack--;
  }

  // R0 — task argument
  *top_of_stack = (StackType_t)task_params;

  return top_of_stack;
}

static void task_exit_trap(void)
{
  // should never reach here
  for ( ; ; ) {
  }
}

StatusCode task_create(TaskFunction task_function, uint16_t stack_depth, TaskPriorityLevel priority, void *task_params, TaskControlBlock **p_task_control_block)
{
  if (task_counter == MAX_NUM_TASKS) {
    return E_RESOURCE_EXHAUSTED;
  }

  if (priority >= NUM_TASK_PRIORITIES) {
    priority = NUM_TASK_PRIORITIES - 1;
  }

  *p_task_control_block = &tcb_pool[task_counter];

  (*p_task_control_block)->stack_base = heap_malloc(sizeof(StackType_t) * stack_depth);
  if ((*p_task_control_block)->stack_base == NULL) {
    uart_print("Could not create task, out of stack space!");
    return E_OUT_OF_MEM;
  }

  for (int i = 0; i < stack_depth; i++) {
    (*p_task_control_block)->stack_base[i] = TASK_WATERMARK;
  }

  (*p_task_control_block)->task_id = task_counter;
  task_counter++;

  (*p_task_control_block)->priority = priority;
  (*p_task_control_block)->base_priority = priority;
  (*p_task_control_block)->mutexes_held = 0;

  (*p_task_control_block)->stack_high = (*p_task_control_block)->stack_base + stack_depth - 1;

  (*p_task_control_block)->current_state = TASK_STATE_READY;
  (*p_task_control_block)->wakeup_time = 0U;
  (*p_task_control_block)->wakeup_reason = WAKEUP_REASON_NONE;

  // initialize stack
  (*p_task_control_block)->current_sp = task_init_stack((*p_task_control_block)->stack_high, task_function, task_params);

  (*p_task_control_block)->state_list_item.owner = *p_task_control_block;
  (*p_task_control_block)->state_list_item.container = NULL;
  (*p_task_control_block)->state_list_item.next = NULL;
  (*p_task_control_block)->state_list_item.prev = NULL;

  (*p_task_control_block)->event_list_item.owner = *p_task_control_block;
  (*p_task_control_block)->event_list_item.container = NULL;
  (*p_task_control_block)->event_list_item.next = NULL;
  (*p_task_control_block)->event_list_item.prev = NULL;

  scheduler_add_to_ready_list(p_task_control_block);

  return E_OK;
}