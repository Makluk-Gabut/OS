#include "vmm.h"
#include "idt.h"
#include "screen.h"
#include <stdint.h>

#define PAGE_PRESENT 0x1
#define PAGE_RW      0x2

// Page directory dan page table pertama harus 4KB-aligned (syarat hardware x86)
static uint32_t page_directory[1024] __attribute__((aligned(4096)));
static uint32_t first_page_table[1024] __attribute__((aligned(4096)));

static void page_fault_handler(struct registers* regs) {
    uint32_t faulting_address;
    asm volatile ("mov %%cr2, %0" : "=r"(faulting_address));

    print_string("\n[PAGE FAULT] alamat=");
    print_hex(faulting_address);
    print_string(" err_code=");
    print_hex(regs->err_code);

    print_string("\n  present=");
    print_string((regs->err_code & 0x1) ? "yes (protection violation)" : "no (halaman belum di-map)");
    print_string("\n  akses=");
    print_string((regs->err_code & 0x2) ? "write" : "read");
    print_string("\n  mode=");
    print_string((regs->err_code & 0x4) ? "user" : "kernel");
    print_string("\nSistem dihentikan.\n");

    for (;;) {
        asm volatile ("cli; hlt");
    }
}

void vmm_init(void) {
    // Identity-map 4MB pertama (0x00000000 - 0x00400000).
    // Cukup buat kernel sekarang + VGA text buffer (0xB8000) otomatis ke-cover di rentang ini.
    for (uint32_t i = 0; i < 1024; i++) {
        first_page_table[i] = (i * 4096) | PAGE_PRESENT | PAGE_RW;
    }

    for (uint32_t i = 0; i < 1024; i++) {
        page_directory[i] = 0; // not present, belum ada mapping lain
    }
    page_directory[0] = ((uint32_t)first_page_table) | PAGE_PRESENT | PAGE_RW;

    register_interrupt_handler(14, page_fault_handler); // exception 14 = Page Fault

    asm volatile ("mov %0, %%cr3" : : "r"(page_directory));

    uint32_t cr0;
    asm volatile ("mov %%cr0, %0" : "=r"(cr0));
    cr0 |= 0x80000000; // set bit 31 (PG) -> paging resmi aktif
    asm volatile ("mov %0, %%cr0" : : "r"(cr0));
}
