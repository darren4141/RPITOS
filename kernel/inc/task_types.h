#ifndef TASK_TYPES_H
#define TASK_TYPES_H

#include <stdint.h>

#define MAX_NUM_TASKS  16
#define TASK_WATERMARK 0x5A

typedef enum {
  TASK_PRIORITY_IDLE = 0,
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

// Forward declarations — all three structs reference each other via pointers
typedef struct List List;
typedef struct ListItem ListItem;
typedef struct TaskControlBlock TaskControlBlock;

typedef uint32_t StackType_t;
typedef void (*TaskFunction_t)(void *);

struct List {
  uint16_t num_items;
  ListItem *head;
  ListItem *index;
  ListItem *list_end;
};

struct ListItem {
  ListItem *next;
  ListItem *prev;
  TaskControlBlock *owner;
  List *container;
};

struct TaskControlBlock {
  volatile StackType_t *p_TopOfStack;
  StackType_t *p_EndOfStack;
  StackType_t *p_Stack;
  uint16_t stackDepth;

  uint16_t taskId;

  volatile TaskState currentState;
  TaskPriorityLevel priority;
  uint64_t wakeup_time;

  ListItem state_list_item;
  ListItem event_list_item;
};

#endif
