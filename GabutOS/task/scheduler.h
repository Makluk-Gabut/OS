#ifndef SCHEDULER_H
#define SCHEDULER_H

#include <stdint.h>
#include "task.h"

#define NUM_PRIORITY_LEVELS 4

void scheduler_init(void);
void scheduler_add_task(struct task* t);
void scheduler_start(struct task* initial_task);
int scheduler_is_active(void);
uint32_t scheduler_tick(uint32_t current_esp);
uint32_t scheduler_maybe_switch(uint32_t current_esp);
uint32_t scheduler_kill_current(uint32_t current_esp);
int scheduler_current_is_killable(void);
void task_sleep_ms(uint32_t ms);
void scheduler_list(void);

#endif
