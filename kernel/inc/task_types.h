#ifndef TASK_TYPES_H
#define TASK_TYPES_H

#include <stdint.h>

#define MAX_NUM_TASKS  16
#define TASK_WATERMARK 0x5A

typedef enum {
  TASK_PRIORITY_0 = 0,
  TASK_PRIORITY_1 = 1,
  TASK_PRIORITY_2 = 2,
  TASK_PRIORITY_3 = 3,
  TASK_PRIORITY_4 = 4,
  TASK_PRIORITY_5 = 5,
  NUM_TASK_PRIORITIES
} TaskPriorityLevel;

typedef enum {
  TASK_STATE_READY,
  TASK_STATE_RUNNING,
  TASK_STATE_BLOCKED,
  TASK_STATE_SUSPENDED,
  TASK_STATE_DELETED
} TaskState;

typedef uint32_t StackType_t;
typedef void (*TaskFunction_t)(void *);

typedef struct TaskControlBlock {
  volatile StackType_t *p_TopOfStack;
  StackType_t *p_EndOfStack;
  StackType_t *p_Stack;
  uint16_t stackDepth;

  uint16_t taskId;

  volatile TaskState currentState;
  TaskPriorityLevel priority;
  uint64_t wakeup_time;

  struct TaskControlBlock *next;
  struct TaskControlBlock *prev;
} TaskControlBlock;

#endif