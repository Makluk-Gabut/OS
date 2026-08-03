#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#include "screen.h"
#include "gdt.h"
#include "idt.h"
#include "keyboard.h"
#include "pit.h"
#include "multiboot.h"
#include "pmm.h"
#include "vmm.h"

// --- UTILS ---
int strcmp(const char* s1, const char* s2) {
    while (*s1 && (*s1 == *s2)) { s1++; s2++; }
    return *(const unsigned char*)s1 - *(const unsigned char*)s2;
}

// --- MEMORY MANAGEMENT (masih bump allocator, belum ada free) ---
#define HEAP_SIZE (1024 * 1024)
static uint8_t heap[HEAP_SIZE];
static uint32_t heap_ptr = 0;

void* malloc(size_t size) {
    heap_ptr = (heap_ptr + 7) & ~7; // 8-byte align
    if (heap_ptr + size >= HEAP_SIZE) return NULL;
    void* res = &heap[heap_ptr];
    heap_ptr += size;
    return res;
}

// --- SHELL ---
static void execute_command(char* cmd) {
    if (strcmp(cmd, "help") == 0) {
        print_string("Available commands: help, clear, mem, uptime, pages\n");
    } else if (strcmp(cmd, "clear") == 0) {
        clear_screen();
    } else if (strcmp(cmd, "pages") == 0) {
        print_string("Physical pages: ");
        print_dec(pmm_free_page_count());
        print_string(" free / ");
        print_dec(pmm_total_page_count());
        print_string(" total (");
        print_dec(pmm_free_page_count() * 4);
        print_string(" KB bebas)\n");
    } else if (strcmp(cmd, "uptime") == 0) {
        uint32_t ticks = pit_get_ticks();
        print_string("Ticks: ");
        print_dec(ticks);
        print_string(" (~");
        print_dec(ticks / 100); // 100Hz -> bagi 100 buat dapet detik
        print_string(" detik sejak boot)\n");
    } else if (strcmp(cmd, "mem") == 0) {
        print_string("Heap used: ");
        print_dec(heap_ptr);
        print_string(" / ");
        print_dec(HEAP_SIZE);
        print_string(" bytes\n");
    } else if (cmd[0] != '\0') {
        print_string("Command not found: ");
        print_string(cmd);
        print_string("\n");
    }
}

static void shell(void) {
    char command[80];
    int i = 0;

    print_string("GabutOS> ");

    while (true) {
        char c = keyboard_getchar(); // blocking, tidur (hlt) sampai IRQ1 ngisi buffer

        if (c == '\n') {
            print_char('\n');
            command[i] = '\0';
            execute_command(command);
            i = 0;
            print_string("GabutOS> ");
        } else if (c == '\b') {
            if (i > 0) {
                i--;
                print_char('\b');
            }
        } else if (c != 0 && i < 79) {
            command[i++] = c;
            print_char(c);
        }
    }
}

void kernel_main(uint32_t magic, uint32_t mb_info_addr) {
    clear_screen();
    print_string("GabutOS v0.2.0 - GDT + IDT + Paging + IRQ-driven\n");
    print_string("=========================================\n");

    gdt_install();
    print_string("[ok] GDT terpasang\n");

    idt_install();
    print_string("[ok] IDT + PIC remap terpasang\n");

    keyboard_install();
    print_string("[ok] Keyboard driver (IRQ1) aktif\n");

    pit_install();
    print_string("[ok] PIT timer (IRQ0, 100Hz) aktif\n");

    if (magic != MULTIBOOT_BOOTLOADER_MAGIC) {
        print_string("[warn] Bukan di-boot GRUB Multiboot valid, PMM di-skip.\n");
    } else {
        struct multiboot_info* mbi = (struct multiboot_info*)mb_info_addr;
        pmm_init(mbi);
        print_string("[ok] Physical Memory Manager aktif (");
        print_dec(pmm_total_page_count() * 4);
        print_string(" KB terdeteksi)\n");

        vmm_init();
        print_string("[ok] Paging aktif (identity-map 4MB pertama)\n");
    }

    asm volatile ("sti"); // baru sekarang aman nyalain interrupt
    print_string("Interrupts ON. Ketik 'help' untuk daftar perintah.\n");

    shell();
}
