#ifndef TASK_H
#define TASK_H

#include <stdint.h>

#define TASK_NAME_LEN 16
#define TASK_KSTACK_SIZE 4096

enum task_state {
    TASK_READY,
    TASK_RUNNING,
    TASK_SLEEPING,
    TASK_DEAD
};

struct task {
    uint32_t esp;
    uint32_t kstack_top;
    int ring;
    enum task_state state;
    int priority;
    uint32_t wake_tick;
    char name[TASK_NAME_LEN];
    struct task* next;
};

struct task* task_create(const char* name, uint32_t entry, int ring, int priority);
struct task* task_create_current(const char* name, int priority);

#endif
