#include "tss.h"
#include "gdt.h"

struct tss_entry {
    uint32_t prev_tss;
    uint32_t esp0;
    uint32_t ss0;
    uint32_t esp1;
    uint32_t ss1;
    uint32_t esp2;
    uint32_t ss2;
    uint32_t cr3;
    uint32_t eip;
    uint32_t eflags;
    uint32_t eax, ecx, edx, ebx;
    uint32_t esp, ebp;
    uint32_t esi, edi;
    uint32_t es, cs, ss, ds, fs, gs;
    uint32_t ldt;
    uint16_t trap;
    uint16_t iomap_base;
} __attribute__((packed));

static struct tss_entry tss;

void tss_install(uint16_t ss0, uint32_t esp0) {
    uint8_t* p = (uint8_t*)&tss;
    for (uint32_t i = 0; i < sizeof(struct tss_entry); i++) p[i] = 0;

    uint32_t base = (uint32_t)&tss;
    uint32_t limit = base + sizeof(struct tss_entry) - 1;
    gdt_set_tss_gate(base, limit);

    tss.ss0 = ss0;
    tss.esp0 = esp0;
    tss.cs = 0x0B;
    tss.ss = tss.ds = tss.es = tss.fs = tss.gs = 0x13;
    tss.iomap_base = sizeof(struct tss_entry);

    asm volatile ("ltr %%ax" : : "a"((uint16_t)0x28));
}

void tss_set_kernel_stack(uint32_t esp0) {
    tss.esp0 = esp0;
}
