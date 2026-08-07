#include "task.h"
#include "heap.h"
#include "pmm.h"
#include "vmm.h"

#define KERNEL_CODE_SEL 0x08
#define KERNEL_DATA_SEL 0x10
#define USER_CODE_SEL   0x1B
#define USER_DATA_SEL   0x23

struct frame_ring0 {
    uint32_t ds;
    uint32_t edi, esi, ebp, esp_dummy, ebx, edx, ecx, eax;
    uint32_t int_no, err_code;
    uint32_t eip, cs, eflags;
};

struct frame_ring3 {
    uint32_t ds;
    uint32_t edi, esi, ebp, esp_dummy, ebx, edx, ecx, eax;
    uint32_t int_no, err_code;
    uint32_t eip, cs, eflags, useresp, ss;
};

static uint32_t next_user_stack_vaddr = 0xB00000u;

static void set_name(struct task* t, const char* name) {
    int i = 0;
    for (; name[i] != '\0' && i < TASK_NAME_LEN - 1; i++) t->name[i] = name[i];
    t->name[i] = '\0';
}

struct task* task_create(const char* name, uint32_t entry, int ring, int priority) {
    struct task* t = (struct task*)kmalloc(sizeof(struct task));
    if (!t) return NULL;

    uint8_t* kstack = (uint8_t*)kmalloc(TASK_KSTACK_SIZE);
    if (!kstack) return NULL;
    uint32_t kstack_top = (uint32_t)kstack + TASK_KSTACK_SIZE;
    t->kstack_top = kstack_top;

    if (ring == 3) {
        uint32_t stack_phys = pmm_alloc_page();
        if (stack_phys == 0) return NULL;

        uint32_t user_stack_vaddr = next_user_stack_vaddr;
        next_user_stack_vaddr += 4096;

        if (!vmm_map_page(user_stack_vaddr, stack_phys, PAGE_PRESENT | PAGE_RW | PAGE_USER)) {
            return NULL;
        }

        struct frame_ring3* frame =
            (struct frame_ring3*)(kstack_top - sizeof(struct frame_ring3));

        frame->ds = USER_DATA_SEL;
        frame->edi = frame->esi = frame->ebp = frame->esp_dummy = 0;
        frame->ebx = frame->edx = frame->ecx = frame->eax = 0;
        frame->int_no = 0;
        frame->err_code = 0;
        frame->eip = entry;
        frame->cs = USER_CODE_SEL;
        frame->eflags = 0x202;
        frame->useresp = user_stack_vaddr + 4096;
        frame->ss = USER_DATA_SEL;

        t->esp = (uint32_t)frame;
    } else {
        struct frame_ring0* frame =
            (struct frame_ring0*)(kstack_top - sizeof(struct frame_ring0));

        frame->ds = KERNEL_DATA_SEL;
        frame->edi = frame->esi = frame->ebp = frame->esp_dummy = 0;
        frame->ebx = frame->edx = frame->ecx = frame->eax = 0;
        frame->int_no = 0;
        frame->err_code = 0;
        frame->eip = entry;
        frame->cs = KERNEL_CODE_SEL;
        frame->eflags = 0x202;

        t->esp = (uint32_t)frame;
    }

    t->ring = ring;
    t->state = TASK_READY;
    t->priority = priority;
    t->wake_tick = 0;
    t->next = NULL;
    set_name(t, name);

    return t;
}

struct task* task_create_current(const char* name, int priority) {
    struct task* t = (struct task*)kmalloc(sizeof(struct task));
    if (!t) return NULL;

    t->esp = 0;
    t->kstack_top = 0;
    t->ring = 0;
    t->state = TASK_RUNNING;
    t->priority = priority;
    t->wake_tick = 0;
    t->next = NULL;
    set_name(t, name);

    return t;
}
