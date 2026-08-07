#include "scheduler.h"
#include "tss.h"
#include "pit.h"
#include "screen.h"

static struct task* ready_head;
static struct task* ready_tail;
static struct task* sleeping_list;
static struct task* current_task;
static int active = 0;
static int pending_switch = 0;

static void enqueue_ready(struct task* t) {
    t->state = TASK_READY;
    t->next = NULL;

    if (ready_tail == NULL) {
        ready_head = t;
        ready_tail = t;
    } else {
        ready_tail->next = t;
        ready_tail = t;
    }
}

static struct task* dequeue_ready(void) {
    if (!ready_head) return NULL;

    struct task* t = ready_head;
    ready_head = t->next;
    if (!ready_head) ready_tail = NULL;
    t->next = NULL;
    return t;
}

static void wake_sleepers(uint32_t now_tick) {
    struct task** pp = &sleeping_list;
    while (*pp) {
        struct task* t = *pp;
        if (t->wake_tick <= now_tick) {
            *pp = t->next;
            enqueue_ready(t);
        } else {
            pp = &t->next;
        }
    }
}

void scheduler_init(void) {
    ready_head = NULL;
    ready_tail = NULL;
    sleeping_list = NULL;
    current_task = NULL;
    active = 0;
}

void scheduler_add_task(struct task* t) {
    enqueue_ready(t);
}

void scheduler_start(struct task* initial_task) {
    current_task = initial_task;
    current_task->state = TASK_RUNNING;
    active = 1;
}

int scheduler_is_active(void) {
    return active;
}

uint32_t scheduler_tick(uint32_t current_esp) {
    if (!active) return current_esp;

    uint32_t now = pit_get_ticks();
    wake_sleepers(now);

    if (current_task && current_task->state == TASK_RUNNING) {
        current_task->esp = current_esp;
        enqueue_ready(current_task);
    }

    struct task* next = dequeue_ready();
    if (!next) {
        return current_esp;
    }

    next->state = TASK_RUNNING;
    current_task = next;

    if (next->ring == 3) {
        tss_set_kernel_stack(next->kstack_top);
    }

    return next->esp;
}

void task_sleep_ms(uint32_t ms) {
    if (!current_task || current_task->state != TASK_RUNNING) return;

    current_task->wake_tick = pit_get_ticks() + (ms / 10);
    current_task->state = TASK_SLEEPING;
    pending_switch = 1;
}

uint32_t scheduler_maybe_switch(uint32_t current_esp) {
    if (!active || !pending_switch) return current_esp;
    pending_switch = 0;

    struct task* sleeper = current_task;
    sleeper->esp = current_esp;
    sleeper->next = sleeping_list;
    sleeping_list = sleeper;

    struct task* next = dequeue_ready();
    if (!next) {
        sleeping_list = sleeper->next;
        sleeper->next = NULL;
        sleeper->state = TASK_RUNNING;
        current_task = sleeper;
        return current_esp;
    }

    next->state = TASK_RUNNING;
    current_task = next;

    if (next->ring == 3) {
        tss_set_kernel_stack(next->kstack_top);
    }

    return next->esp;
}

void scheduler_list(void) {
    if (current_task) {
        print_string("  * ");
        print_string(current_task->name);
        print_string(" (running, ring");
        print_dec((uint32_t)current_task->ring);
        print_string(", prio ");
        print_dec((uint32_t)current_task->priority);
        print_string(")\n");
    }

    struct task* t = ready_head;
    while (t) {
        print_string("    ");
        print_string(t->name);
        print_string(" (ready, ring");
        print_dec((uint32_t)t->ring);
        print_string(", prio ");
        print_dec((uint32_t)t->priority);
        print_string(")\n");
        t = t->next;
    }

    struct task* s = sleeping_list;
    while (s) {
        print_string("    ");
        print_string(s->name);
        print_string(" (sleeping sampai tick ");
        print_dec(s->wake_tick);
        print_string(")\n");
        s = s->next;
    }
}
