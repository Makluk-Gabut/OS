#include "usermode.h"
#include "idt.h"
#include "screen.h"
#include "pmm.h"
#include "vmm.h"
#include <stdint.h>

#define SYS_PRINT 1
#define USER_STACK_VADDR 0xA00000u

extern void enter_usermode(uint32_t entry_eip, uint32_t user_stack);

static void syscall_handler(struct registers* regs) {
    if (regs->eax == SYS_PRINT) {
        print_char((char)regs->ebx);
    } else {
        print_string("[syscall] unknown: ");
        print_hex(regs->eax);
        print_string("\n");
    }
}

void usermode_install(void) {
    register_interrupt_handler(128, syscall_handler);
}

static void ring3_task(void) {
    const char msg[] = "Hello from ring3! CPL beneran ke-3, bukan cuma iret doang.\n";
    for (int i = 0; msg[i]; i++) {
        asm volatile ("int $0x80" : : "a"(SYS_PRINT), "b"(msg[i]));
    }

    asm volatile ("cli");

    for (;;) {
    }
}

void usermode_jump(uint32_t entry_eip) {
    uint32_t stack_phys = pmm_alloc_page();
    if (stack_phys == 0) {
        print_string("[usermode] gagal alokasi stack fisik\n");
        return;
    }

    if (!vmm_map_page(USER_STACK_VADDR, stack_phys, PAGE_PRESENT | PAGE_RW | PAGE_USER)) {
        print_string("[usermode] gagal map stack usermode\n");
        return;
    }

    enter_usermode(entry_eip, USER_STACK_VADDR + 4096);
}

void usermode_run_demo(void) {
    print_string("[usermode] lompat ke ring3...\n");
    usermode_jump((uint32_t)ring3_task);
}
