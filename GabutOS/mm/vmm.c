#include "vmm.h"
#include "idt.h"
#include "screen.h"
#include "pmm.h"
#include <stdint.h>

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

    for (uint32_t i = 0; i < 1024; i++) {
        first_page_table[i] = (i * 4096) | PAGE_PRESENT | PAGE_RW | PAGE_USER;
    }

    for (uint32_t i = 0; i < 1024; i++) {
        page_directory[i] = 0;
    }
    page_directory[0] = ((uint32_t)first_page_table) | PAGE_PRESENT | PAGE_RW | PAGE_USER;

    register_interrupt_handler(14, page_fault_handler);

    asm volatile ("mov %0, %%cr3" : : "r"(page_directory));

    uint32_t cr0;
    asm volatile ("mov %%cr0, %0" : "=r"(cr0));
    cr0 |= 0x80000000;
    asm volatile ("mov %0, %%cr0" : : "r"(cr0));
}

#define IDENTITY_MAP_LIMIT 0x400000u

uint32_t vmm_map_page(uint32_t virtual_addr, uint32_t physical_addr, uint32_t flags) {
    uint32_t pd_index = (virtual_addr >> 22) & 0x3FF;
    uint32_t pt_index = (virtual_addr >> 12) & 0x3FF;

    if (!(page_directory[pd_index] & PAGE_PRESENT)) {
        uint32_t table_phys = pmm_alloc_page();
        if (table_phys == 0) return 0;

        if (table_phys >= IDENTITY_MAP_LIMIT) {

            print_string("[vmm] PANIC: butuh page table di luar identity-map (belum didukung)\n");
            for (;;) asm volatile ("cli; hlt");
        }

        uint32_t* new_table = (uint32_t*)table_phys;
        for (int i = 0; i < 1024; i++) new_table[i] = 0;

        page_directory[pd_index] = (table_phys & 0xFFFFF000) | PAGE_PRESENT | PAGE_RW | PAGE_USER;
    }

    uint32_t* table = (uint32_t*)(page_directory[pd_index] & 0xFFFFF000);
    table[pt_index] = (physical_addr & 0xFFFFF000) | flags;

    asm volatile ("invlpg (%0)" : : "r"(virtual_addr) : "memory");
    return 1;
}

int vmm_is_mapped(uint32_t virtual_addr) {
    uint32_t pd_index = (virtual_addr >> 22) & 0x3FF;
    uint32_t pt_index = (virtual_addr >> 12) & 0x3FF;

    if (!(page_directory[pd_index] & PAGE_PRESENT)) return 0;

    uint32_t* table = (uint32_t*)(page_directory[pd_index] & 0xFFFFF000);
    return (table[pt_index] & PAGE_PRESENT) != 0;
}
