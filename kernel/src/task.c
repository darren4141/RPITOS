#include "task.h"

static uint16_t taskCounter = 0;

static StackType_t *initializeTaskStack(StackType_t *topOfStack, TaskFunction_t taskFunction, void *taskParams)
{
  *topOfStack = 0x01000000;   // xPSR  — Thumb bit set
  topOfStack--;

  *topOfStack = (StackType_t)taskFunction;
  topOfStack--;

  *topOfStack = (StackType_t)prvTaskExitTrap;
  topOfStack--;

  *topOfStack = 0;                       // R12
  topOfStack--;

  *topOfStack = 0;                       // R3
  topOfStack--;
  *topOfStack = 0;                       // R2
  topOfStack--;
  *topOfStack = 0;                       // R1
  topOfStack--;

  *topOfStack = (StackType_t)taskParams; // R0   — task argument
  topOfStack--;

  // software-saved registers (PendSV pushes these manually)
  *topOfStack = 0;                       // R11
  topOfStack--;
  *topOfStack = 0;                       // R10
  topOfStack--;
  *topOfStack = 0;                       // R9
  topOfStack--;
  *topOfStack = 0;                       // R8
  topOfStack--;
  *topOfStack = 0;                       // R7
  topOfStack--;
  *topOfStack = 0;                       // R6
  topOfStack--;
  *topOfStack = 0;                       // R5
  topOfStack--;
  *topOfStack = 0;                       // R4

  return topOfStack;
}

static void prvTaskExitTrap(void)
{
  // should never reach here
  for ( ; ; ) {
  }
}

StatusCode task_create(TaskFunction_t taskFunction, uint16_t stack_depth, TaskPriorityLevel priority, void *taskParams, TaskControlBlock *p_task_control_block)
{
  if (taskCounter == MAX_NUM_TASKS) {
    return E_RESOURCE_EXHAUSTED;
  }

  if (priority >= NUM_TASK_PRIORITIES) {
    priority = NUM_TASK_PRIORITIES - 1;
  }

  p_task_control_block = heap_malloc(sizeof(TaskControlBlock));
  if (p_task_control_block == NULL) {
    return E_OUT_OF_MEM;
  }

  *(p_task_control_block->p_Stack) = heap_malloc(sizeof(StackType_t) * stack_depth);
  if (p_task_control_block->p_Stack == NULL) {
    return E_OUT_OF_MEM;
  }

  for (int i = 0; i < stack_depth; i++) {
    p_task_control_block->p_Stack[i] = TASK_WATERMARK;
  }

  p_task_control_block->taskId = taskCounter;
  taskCounter++;

  p_task_control_block->priority = priority;

  p_task_control_block->p_EndOfStack = p_task_control_block->p_Stack + stack_depth - 1;

  p_task_control_block->currentState = TASK_STATE_READY;
  p_task_control_block->ticksToWait = 0U;

  // initialize stack
  p_task_control_block->p_TopOfStack = initializeTaskStack(p_task_control_block->p_EndOfStack, taskFunction, taskParams);

  p_task_control_block->next = NULL;
  p_task_control_block->prev = NULL;

  addToReadyList(p_task_control_block);

  return E_OK;
}