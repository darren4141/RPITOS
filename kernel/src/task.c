#include "task.h"
#include "uart.h"

#include <stddef.h>

static uint16_t taskCounter = 0;

static StackType_t *initializeTaskStack(StackType_t *topOfStack, TaskFunction_t taskFunction, void *taskParams)
{
  // SPSR — SVC mode, interrupts enabled, Thumb or ARM
  *topOfStack = 0x00000013;      // SVC mode, ARM state, IRQs enabled
  topOfStack--;

  // PC — task entry point
  *topOfStack = (StackType_t)taskFunction;
  topOfStack--;

  // R12 down to R1 — all zero
  for (int i = 12; i >= 1; i--) {
    *topOfStack = 0;
    topOfStack--;
  }

  // R0 — task argument
  *topOfStack = (StackType_t)taskParams;

  return topOfStack;
}

static void prvTaskExitTrap(void)
{
  // should never reach here
  for ( ; ; ) {
  }
}

StatusCode task_create(TaskFunction_t taskFunction, uint16_t stack_depth, TaskPriorityLevel priority, void *taskParams, TaskControlBlock **p_task_control_block)
{
  if (taskCounter == MAX_NUM_TASKS) {
    return E_RESOURCE_EXHAUSTED;
  }

  if (priority >= NUM_TASK_PRIORITIES) {
    priority = NUM_TASK_PRIORITIES - 1;
  }

  *p_task_control_block = heap_malloc(sizeof(TaskControlBlock));
  if (*p_task_control_block == NULL) {
    uart_print("Could not create task, out of stack space!");
    return E_OUT_OF_MEM;
  }

  (*p_task_control_block)->p_Stack = heap_malloc(sizeof(StackType_t) * stack_depth);
  if ((*p_task_control_block)->p_Stack == NULL) {
    uart_print("Could not create task, out of stack space!");
    return E_OUT_OF_MEM;
  }

  for (int i = 0; i < stack_depth; i++) {
    (*p_task_control_block)->p_Stack[i] = TASK_WATERMARK;
  }

  (*p_task_control_block)->taskId = taskCounter;
  taskCounter++;

  (*p_task_control_block)->priority = priority;

  (*p_task_control_block)->p_EndOfStack = (*p_task_control_block)->p_Stack + stack_depth - 1;

  (*p_task_control_block)->currentState = TASK_STATE_READY;
  (*p_task_control_block)->wakeup_time = 0U;

  // initialize stack
  (*p_task_control_block)->p_TopOfStack = initializeTaskStack((*p_task_control_block)->p_EndOfStack, taskFunction, taskParams);

  (*p_task_control_block)->state_list_item.owner = *p_task_control_block;
  (*p_task_control_block)->state_list_item.container = NULL;
  (*p_task_control_block)->state_list_item.next = NULL;
  (*p_task_control_block)->state_list_item.prev = NULL;

  (*p_task_control_block)->event_list_item.owner = *p_task_control_block;
  (*p_task_control_block)->event_list_item.container = NULL;
  (*p_task_control_block)->event_list_item.next = NULL;
  (*p_task_control_block)->event_list_item.prev = NULL;

  addToReadyList(p_task_control_block);

  return E_OK;
}