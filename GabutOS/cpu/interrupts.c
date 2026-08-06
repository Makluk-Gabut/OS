#include "idt.h"
#include "io.h"
#include "screen.h"

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

void isr_handler(struct registers regs) {
    if (interrupt_handlers[regs.int_no] != 0) {
        interrupt_handlers[regs.int_no](&regs);
        return;
    }

    print_string("\n[PANIC] Exception: ");
    if (regs.int_no < 32) {
        print_string(exception_messages[regs.int_no]);
    } else {
        print_hex(regs.int_no);
    }
    print_string("  err_code=");
    print_hex(regs.err_code);
    print_string("\nSistem dihentikan.\n");

    for (;;) {
        asm volatile ("cli; hlt");
    }
}

void irq_handler(struct registers regs) {

    if (regs.int_no >= 40) {
        outb(0xA0, 0x20);
    }
    outb(0x20, 0x20);

    if (interrupt_handlers[regs.int_no] != 0) {
        interrupt_handlers[regs.int_no](&regs);
    }
}
