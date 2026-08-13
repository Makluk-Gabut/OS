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
#include "fs.h"
#include "shell.h"

extern uint32_t stack_top;

void kernel_main(uint32_t magic, uint32_t mb_info_addr) {
    serial_init();
    serial_write("=== GabutOS booting ===\n");

    clear_screen();
    print_string("GabutOS v1.1.0 - Shell Rewrite (argc/argv)\n");
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

        if (fs_mount()) {
            print_string("[ok] Filesystem di-mount dari disk\n");
        } else {
            print_string("[warn] Filesystem belum ada, jalankan 'fsformat' dulu\n");
        }
    }

    asm volatile ("sti");
    print_string("Interrupts ON. Ketik 'help' untuk daftar perintah.\n");

    shell_run();
}
