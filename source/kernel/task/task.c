#include "task.h"
#include "uart.h"

#include <stddef.h>

static uint16_t taskCounter = 0;

// TCBs live in a dedicated static pool, never adjacent to any task stack.
// This means no amount of stack overflow can corrupt a TCB.
static TaskControlBlock tcb_pool[MAX_NUM_TASKS];

static void prvTaskExitTrap(void);

static StackType_t *initializeTaskStack(StackType_t *topOfStack, TaskFunction_t taskFunction, void *taskParams)
{
  // SPSR — SVC mode, interrupts enabled, Thumb or ARM
  *topOfStack = 0x00000013;      // SVC mode, ARM state, IRQs enabled
  topOfStack--;

  // PC — task entry point
  *topOfStack = (StackType_t)taskFunction;
  topOfStack--;

  // LR — must match the [r0-r12][lr][pc][spsr] frame that _irq_handler saves
  // with push {r0-r12, lr}. Points at the exit trap so a task function that
  // returns is caught instead of branching to garbage.
  *topOfStack = (StackType_t)prvTaskExitTrap;
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

// Free stack headroom in words: count intact watermark words from the bottom up
// to the high-water line. Shrinks monotonically under a real leak; stays flat if
// the bottom is clobbered by a stray write from elsewhere.
uint32_t task_stack_free_words(const TaskControlBlock *tcb)
{
  uint32_t depth = (uint32_t)(tcb->p_EndOfStack - tcb->p_Stack + 1);
  uint32_t freeWords = 0;
  while (freeWords < depth && tcb->p_Stack[freeWords] == TASK_WATERMARK) {
    freeWords++;
  }
  return freeWords;
}

// ── Stack-overflow guard (fault-safe direct UART) ─────────────────────────────
static void task_puthex(uint32_t v)
{
  static const char h[] = "0123456789ABCDEF";
  uart_tx_raw('0');
  uart_tx_raw('x');
  for (int i = 28; i >= 0; i -= 4) {
    uart_tx_raw((uint8_t)h[(v >> i) & 0xFU]);
  }
}

// Checks the bottom-of-stack watermark of every task. p_Stack[0] is the lowest
// word of a task's stack (stacks grow down toward it); if it's no longer
// TASK_WATERMARK, that task has consumed its entire stack and is overflowing
// into whatever sits below it. Reports the culprit over UART and halts — called
// from the scheduler tick, so it uses uart_tx_raw only (no DMA/task path).
void task_check_stacks(void)
{
  for (uint16_t i = 0; i < taskCounter; i++) {
    if (tcb_pool[i].p_Stack[0] != TASK_WATERMARK) {
      uart_tx_quiesce();   // stop in-flight DMA so the report isn't garbled
      // Print the clobber VALUE first — it identifies the writer (e.g. a
      // 0xB007xxxx boot-flags constant) even if the line gets truncated.
      const char *s = "\r\nCORRUPT val=";
      while (*s) { uart_tx_raw((uint8_t)*s++); }
      task_puthex(tcb_pool[i].p_Stack[0]);
      s = " at=";
      while (*s) { uart_tx_raw((uint8_t)*s++); }
      task_puthex((uint32_t)&tcb_pool[i].p_Stack[0]);
      s = " id=";
      while (*s) { uart_tx_raw((uint8_t)*s++); }
      task_puthex(i);
      uart_tx_raw('\r');
      uart_tx_raw('\n');
      for ( ; ; ) {}
    }
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

  *p_task_control_block = &tcb_pool[taskCounter];

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
  (*p_task_control_block)->base_priority = priority;
  (*p_task_control_block)->mutexes_held = 0;

  (*p_task_control_block)->p_EndOfStack = (*p_task_control_block)->p_Stack + stack_depth - 1;

  (*p_task_control_block)->currentState = TASK_STATE_READY;
  (*p_task_control_block)->wakeup_time = 0U;
  (*p_task_control_block)->wakeup_reason = WAKEUP_REASON_NONE;

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