#include "companion_core.h"
#include "interrupts.h"
#include "task.h"
#include "telemetry.h"
#include "uart.h"

#include <stddef.h>

// Each core gets its own TCB pool — task_create() always creates a task on
// the calling core (there is no cross-core task-creation API), so these are
// indexed by companion_core_id(), never contended across cores.
static uint16_t task_counter[COMPANION_CORE_MAX_CORES] = { 0 };

static TaskControlBlock tcb_pool[COMPANION_CORE_MAX_CORES][MAX_NUM_TASKS];

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

StatusCode task_create(TaskFunction task_function, uint16_t stack_depth, TaskPriorityLevel priority, void *task_params, const char *name, TaskControlBlock **p_task_control_block)
{
  uint32_t core_id = companion_core_id();

  if (task_counter[core_id] == MAX_NUM_TASKS) {
    return E_RESOURCE_EXHAUSTED;
  }

  if (priority >= NUM_TASK_PRIORITIES) {
    priority = NUM_TASK_PRIORITIES - 1;
  }

  *p_task_control_block = &tcb_pool[core_id][task_counter[core_id]];

  (*p_task_control_block)->stack_base = heap_malloc(sizeof(StackType_t) * stack_depth);
  if ((*p_task_control_block)->stack_base == NULL) {
    uart_print("Could not create task, out of stack space!");
    return E_OUT_OF_MEM;
  }

  for (int i = 0; i < stack_depth; i++) {
    (*p_task_control_block)->stack_base[i] = TASK_WATERMARK;
  }

  (*p_task_control_block)->task_id = task_counter[core_id];
  (*p_task_control_block)->core_id = core_id;
  task_counter[core_id]++;

#ifdef RTOS_TELEMETRY
  // Mask IRQs around the telemetry_lock-guarded send — see
  // md/client/device/instrumentation.md for why this call site needs it.
  uint32_t telemetry_cpsr = enter_critical();
  telemetry_report_task_created((*p_task_control_block)->task_id, core_id, (uint8_t)priority, name);
  exit_critical(telemetry_cpsr);
#else
  (void)name;
#endif

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

  // scheduler_lock() here guards against a concurrent cross-core wake
  // (semaphore_give()/mutex_unlock() from another core) touching this same
  // core's ready list at the same instant — see scheduler_lock()'s doc.
  scheduler_lock(core_id);
  scheduler_add_to_ready_list(p_task_control_block);
  scheduler_unlock(core_id);

  return E_OK;
}