#ifndef SCHEDULER_H
#define SCHEDULER_H

#include "status.h"
#include "task_types.h"

extern TaskControlBlock *p_task_control_block;

StatusCode scheduler_init();

StatusCode schedulerStart(void);

StatusCode addToReadyList(TaskControlBlock **tcb);

void schedulerSwitchContext();

void startFirstTask(void);   // defined in startup.s

#endif