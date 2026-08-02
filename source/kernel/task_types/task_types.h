#ifndef TASK_TYPES_H
#define TASK_TYPES_H

#include <stdint.h>

#define MAX_NUM_TASKS  16
#define TASK_WATERMARK 0x5A
#define TASK_NAME_MAX  16

typedef enum {
  TASK_PRIORITY_IDLE = 0,
  TASK_PRIORITY_1    = 1,
  TASK_PRIORITY_2    = 2,
  TASK_PRIORITY_3    = 3,
  TASK_PRIORITY_4    = 4,
  TASK_PRIORITY_5    = 5,
  NUM_TASK_PRIORITIES
} TaskPriorityLevel;

typedef enum {
  TASK_STATE_READY,
  TASK_STATE_RUNNING,
  TASK_STATE_BLOCKED,
  TASK_STATE_SUSPENDED,
  TASK_STATE_DELETED
} TaskState;

typedef enum {
  WAKEUP_REASON_NONE = 0,
  WAKEUP_REASON_RESOURCE_ACQUIRED,
} TaskWakeupReason;

// Forward declarations — all three structs reference each other via pointers
typedef struct List List;
typedef struct ListItem ListItem;
typedef struct TaskControlBlock TaskControlBlock;

typedef uint32_t StackType_t;

/**
 * @brief Signature for a task entry function, as passed to task_create().
 */
typedef void (*TaskFunction)(void *);

/**
 * @brief Intrusive doubly-linked list of ListItems, used for both the per-priority ready lists and the blocked list.
 */
struct List {
  uint16_t num_items;
  ListItem *head;
  ListItem *index;
  ListItem *list_end;
};

/**
 * @brief One node in a List. Each TaskControlBlock embeds two: state_list_item (ready/blocked) and event_list_item (mutex/semaphore wait).
 */
struct ListItem {
  ListItem *next;
  ListItem *prev;
  TaskControlBlock *owner;
  List *container;
};

/**
 * @brief Per-task state: stack bounds, priority, scheduling state, and the list nodes used to place it in the scheduler's lists.
 */
struct TaskControlBlock {
  volatile StackType_t *current_sp;  // live stack pointer, saved/restored on every context switch
  StackType_t *stack_high;           // highest address in the allocated stack buffer (initial SP value)
  StackType_t *stack_base;           // lowest address in the allocated stack buffer; used for the watermark check
  uint16_t stack_depth;

  uint16_t task_id;
  uint32_t core_id;                  // which core's scheduler (ready/blocked lists) this task belongs to
  char name[TASK_NAME_MAX];          // set at task_create(); always NULL-terminated

  volatile TaskState current_state;
  TaskPriorityLevel priority;
  uint64_t wakeup_time;

  ListItem state_list_item;
  ListItem event_list_item;

  volatile TaskWakeupReason wakeup_reason;

  TaskPriorityLevel base_priority; // original priority, never mutated; used to restore after inheritance boost
  uint32_t mutexes_held;           // count of inheritance-enabled mutexes currently held
};

#endif
