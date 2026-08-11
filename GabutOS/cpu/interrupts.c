#include "idt.h"
#include "io.h"
#include "screen.h"
#include "scheduler.h"

extern isr_t interrupt_handlers[256];

static const char* exception_messages[32] = {
    "Division By Zero", "Debug", "Non Maskable Interrupt", "Breakpoint",
    "Into Detected Overflow", "Out of Bounds", "Invalid Opcode", "No Coprocessor",
    "Double Fault", "Coprocessor Segment Overrun", "Bad TSS", "Segment Not Present",
    "Stack Fault", "General Protection Fault", "Page Fault", "Unknown Interrupt",
    "Coprocessor Fault", "Alignment Check", "Machine Check", "SIMD FP Exception",
    "Virtualization Exception", "Reserved", "Reserved", "Reserved",
    "Reserved", "Reserved", "Reserved", "Reserved",
    "Reserved", "Reserved", "Security Exception", "Reserved"
};

uint32_t isr_handler(struct registers* regs) {
    if (interrupt_handlers[regs->int_no] != 0) {
        interrupt_handlers[regs->int_no](regs);

        if (regs->int_no == 128) {
            return scheduler_maybe_switch((uint32_t)regs);
        }

        if (regs->int_no < 32 && scheduler_current_is_killable()) {
            return scheduler_kill_current((uint32_t)regs);
        }

        return (uint32_t)regs;
    }

    if (scheduler_current_is_killable()) {
        print_string("\n[TASK KILLED] Exception: ");
        if (regs->int_no < 32) {
            print_string(exception_messages[regs->int_no]);
        } else {
            print_hex(regs->int_no);
        }
        print_string("  err_code=");
        print_hex(regs->err_code);
        print_string("\nTask dihentikan, lanjut ke task lain.\n");

        return scheduler_kill_current((uint32_t)regs);
    }

    print_string("\n[PANIC] Exception: ");
    if (regs->int_no < 32) {
        print_string(exception_messages[regs->int_no]);
    } else {
        print_hex(regs->int_no);
    }
    print_string("  err_code=");
    print_hex(regs->err_code);
    print_string("\nSistem dihentikan.\n");

    for (;;) {
        asm volatile ("cli; hlt");
    }
}

uint32_t irq_handler(struct registers* regs) {
    if (regs->int_no >= 40) {
        outb(0xA0, 0x20);
    }
    outb(0x20, 0x20);

    if (interrupt_handlers[regs->int_no] != 0) {
        interrupt_handlers[regs->int_no](regs);
    }

    if (regs->int_no == 32) {
        return scheduler_tick((uint32_t)regs);
    }

    return (uint32_t)regs;
}
