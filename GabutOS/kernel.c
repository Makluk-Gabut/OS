#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#include "screen.h"
#include "serial.h"
#include "gdt.h"
#include "idt.h"
#include "keyboard.h"
#include "pit.h"
#include "multiboot.h"
#include "pmm.h"
#include "vmm.h"
#include "heap.h"
#include "tss.h"
#include "usermode.h"

extern uint32_t stack_top;

int strcmp(const char* s1, const char* s2) {
    while (*s1 && (*s1 == *s2)) { s1++; s2++; }
    return *(const unsigned char*)s1 - *(const unsigned char*)s2;
}

static void execute_command(char* cmd) {
    if (strcmp(cmd, "help") == 0) {
        print_string("Available commands: help, clear, mem, uptime, pages, alloctest, usermode\n");
    } else if (strcmp(cmd, "usermode") == 0) {
        usermode_run_demo();
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
        print_dec(ticks / 100);
        print_string(" detik sejak boot)\n");
    } else if (strcmp(cmd, "mem") == 0) {
        print_string("Heap: dipakai=");
        print_dec((uint32_t)heap_used_bytes());
        print_string("B  bebas=");
        print_dec((uint32_t)heap_free_bytes());
        print_string("B  ter-mapping=");
        print_dec((uint32_t)heap_mapped_bytes());
        print_string("B\n");
    } else if (strcmp(cmd, "alloctest") == 0) {
        print_string("Alokasi A(64B) B(128B) C(32B)...\n");
        void* a = kmalloc(64);
        void* b = kmalloc(128);
        void* c = kmalloc(32);
        print_string("  A="); print_hex((uint32_t)a);
        print_string(" B="); print_hex((uint32_t)b);
        print_string(" C="); print_hex((uint32_t)c);
        print_string("\n");

        print_string("Bebasin B...\n");
        kfree(b);

        print_string("Alokasi D(100B) -- kalau muat, harusnya numpang di bekas slot B\n");
        void* d = kmalloc(100);
        print_string("  D="); print_hex((uint32_t)d); print_string("\n");
        if (d == b) {
            print_string("  -> Cocok! D dapat alamat yang sama persis kayak B lama.\n");
        } else {
            print_string("  -> D dapat alamat baru (mungkin karena D lebih besar dari sisa slot B).\n");
        }

        kfree(a);
        kfree(c);
        kfree(d);
        print_string("Semua di-free lagi.\n");
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
        char c = keyboard_getchar();

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
    serial_init();
    serial_write("=== GabutOS booting ===\n");

    clear_screen();
    print_string("GabutOS v0.4.0 - Ring 0 -> Ring 3 + Syscall\n");
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

        heap_init();
        print_string("[ok] Heap kmalloc/kfree (free-list) siap\n");

        tss_install(0x10, (uint32_t)&stack_top);
        print_string("[ok] TSS terpasang\n");

        usermode_install();
        print_string("[ok] Syscall (int 0x80) siap, ketik 'usermode' buat coba ring3\n");
    }

    asm volatile ("sti");
    print_string("Interrupts ON. Ketik 'help' untuk daftar perintah.\n");

    shell();
}
