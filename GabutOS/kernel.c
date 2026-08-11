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
#include "fs.h"
#include "elf.h"
#include "test_program.h"
#include "demo_task_a.h"
#include "demo_task_b.h"
#include "demo_task_c.h"
#include "demo_task_crash.h"
#include "demo_iso_x.h"
#include "demo_iso_y.h"
#include "demo_crash_iso.h"
#include "demo_hog.h"
#include "task.h"
#include "scheduler.h"

#define RUN_TASK_MAX_PAGES 64

extern uint32_t stack_top;

int strcmp(const char* s1, const char* s2) {
    while (*s1 && (*s1 == *s2)) { s1++; s2++; }
    return *(const unsigned char*)s1 - *(const unsigned char*)s2;
}

static uint32_t strlen_local(const char* s) {
    uint32_t n = 0;
    while (s[n] != '\0') n++;
    return n;
}

static int starts_with(const char* str, const char* prefix) {
    while (*prefix) {
        if (*str != *prefix) return 0;
        str++;
        prefix++;
    }
    return 1;
}

static void execute_command(char* cmd) {
    if (strcmp(cmd, "help") == 0) {
        print_string("Available commands: help, clear, mem, uptime, pages, alloctest, usermode, fsformat, ls, cat <nama>, rm <nama>, fstest, loadtest, run <nama>, multitask, ps, crashtest, isotest\n");
    } else if (strcmp(cmd, "isotest") == 0) {
        if (!scheduler_is_active()) {
            print_string("Jalankan 'multitask' dulu buat nyalain scheduler-nya.\n");
        } else {
            uint32_t* pd_x = vmm_create_address_space();
            uint32_t* pd_y = vmm_create_address_space();

            if (!pd_x || !pd_y) {
                print_string("Gagal bikin address space baru (kehabisan RAM fisik?)\n");
            } else {
                uint32_t entry_x, entry_y;
                int ok_x = elf_load(demo_iso_x_elf, demo_iso_x_elf_size, &entry_x, 3, pd_x, 0);
                int ok_y = elf_load(demo_iso_y_elf, demo_iso_y_elf_size, &entry_y, 3, pd_y, 0);

                if (ok_x && ok_y) {
                    struct task* task_x = task_create("task-X(iso)", entry_x, 3, 1, pd_x);
                    struct task* task_y = task_create("task-Y(iso)", entry_y, 3, 1, pd_y);

                    if (!task_x || !task_y) {
                        print_string("Gagal bikin task (kehabisan heap?)\n");
                    } else {
                        scheduler_add_task(task_x);
                        scheduler_add_task(task_y);

                        print_string("task-X dan task-Y ditambahkan, DUA-DUANYA di-load ke alamat\n");
                        print_string("virtual yang SAMA (0x1000000), tapi di page directory TERPISAH.\n");
                        print_string("Kalau isolasi beneran jalan: X terus ngeprint 'X', Y terus ngeprint 'Y',\n");
                        print_string("gak ada yang ke-corrupt walau alamatnya identik.\n");
                    }
                }
            }
        }
    } else if (strcmp(cmd, "crashtest") == 0) {
        if (!scheduler_is_active()) {
            print_string("Jalankan 'multitask' dulu, baru 'crashtest' buat lihat task lain tetap hidup.\n");
        } else {
            uint32_t entry_crash;
            if (elf_load(demo_task_crash_elf, demo_task_crash_elf_size, &entry_crash, 3, NULL, 0)) {
                struct task* bad_task = task_create("task-CRASH(r3)", entry_crash, 3, 1, NULL);
                scheduler_add_task(bad_task);
                print_string("task-CRASH ditambahkan -- dia bakal nulis ke alamat gak valid dan crash.\n");
                print_string("Perhatiin: task lain (A/B/C) harusnya tetap jalan normal setelahnya.\n");
            }
        }
    } else if (strcmp(cmd, "multitask") == 0) {
        if (scheduler_is_active()) {
            print_string("Scheduler udah jalan. Reboot buat coba dari awal.\n");
        } else {
            scheduler_init();

            struct task* shell_task = task_create_current("shell", 0);

            uint32_t entry_a, entry_b, entry_c;
            elf_load(demo_task_a_elf, demo_task_a_elf_size, &entry_a, 3, NULL, 0);
            elf_load(demo_task_b_elf, demo_task_b_elf_size, &entry_b, 3, NULL, 0);
            elf_load(demo_task_c_elf, demo_task_c_elf_size, &entry_c, 0, NULL, 0);

            struct task* task_a = task_create("task-A(r3)", entry_a, 3, 1, NULL);
            struct task* task_b = task_create("task-B(r3)", entry_b, 3, 2, NULL);
            struct task* task_c = task_create("task-C(r0)", entry_c, 0, 1, NULL);

            scheduler_add_task(task_a);
            scheduler_add_task(task_b);
            scheduler_add_task(task_c);

            print_string("3 task ditambahkan (A=ring3 prio1, B=ring3 prio2, C=ring0 prio1).\n");
            print_string("Scheduler aktif -- huruf A/B/C bakal muncul berselang-seling.\n");
            print_string("Shell (task ini) tetap bisa dipakai normal sambil task lain jalan.\n");

            scheduler_start(shell_task);
        }
    } else if (strcmp(cmd, "ps") == 0) {
        if (!scheduler_is_active()) {
            print_string("Scheduler belum aktif. Jalankan 'multitask' dulu.\n");
        } else {
            scheduler_list();
        }
    } else if (strcmp(cmd, "loadtest") == 0) {
        if (fs_write("hello.elf", test_program_elf, test_program_elf_size)) {
            print_string("hello.elf (");
            print_dec(test_program_elf_size);
            print_string(" bytes) ditulis ke disk.\n");
        } else {
            print_string("Gagal nulis (tabel file penuh atau belum di-format?)\n");
        }
        if (fs_write("crash.elf", demo_crash_iso_elf, demo_crash_iso_elf_size)) {
            print_string("crash.elf (");
            print_dec(demo_crash_iso_elf_size);
            print_string(" bytes) ditulis ke disk -- coba 'run crash.elf' buat lihat reclaim jalan.\n");
        }
        if (fs_write("hog.elf", demo_hog_elf, demo_hog_elf_size)) {
            print_string("hog.elf (");
            print_dec(demo_hog_elf_size);
            print_string(" bytes) ditulis ke disk -- coba 'run hog.elf' buat lihat quota nolak dia.\n");
        }
    } else if (starts_with(cmd, "run ")) {
        const char* filename = cmd + 4;

        if (!scheduler_is_active()) {
            print_string("Jalankan 'multitask' dulu (nyalain scheduler), baru 'run <nama>' bisa dipakai.\n");
        } else {
            uint32_t buf_cap = 65536;
            uint8_t* buf = (uint8_t*)kmalloc(buf_cap);
            if (!buf) {
                print_string("Gagal alokasi buffer buat load file.\n");
            } else {
                uint32_t size;
                if (!fs_read(filename, buf, buf_cap, &size)) {
                    print_string("File gak ketemu: ");
                    print_string(filename);
                    print_string("\n");
                    kfree(buf);
                } else {
                    if (size > buf_cap) {
                        print_string("[warn] file lebih besar dari buffer loader (64KB), kepotong.\n");
                        size = buf_cap;
                    }

                    uint32_t* pd = vmm_create_address_space();
                    if (!pd) {
                        print_string("Gagal bikin address space (kehabisan RAM fisik?)\n");
                        kfree(buf);
                    } else {
                        uint32_t entry;
                        if (!elf_load(buf, size, &entry, 3, pd, RUN_TASK_MAX_PAGES)) {
                            print_string("[elf] gagal load (lihat pesan error di atas)\n");
                            vmm_destroy_address_space(pd);
                        } else {
                            struct task* t = task_create(filename, entry, 3, 1, pd);
                            if (!t) {
                                print_string("Gagal bikin task (kehabisan heap?)\n");
                                vmm_destroy_address_space(pd);
                            } else {
                                scheduler_add_task(t);
                                print_string("[elf] '");
                                print_string(filename);
                                print_string("' jalan sebagai task terisolasi (page directory sendiri).\n");
                            }
                        }
                        kfree(buf);
                    }
                }
            }
        }
    } else if (strcmp(cmd, "fsformat") == 0) {
        fs_format();
        print_string("Filesystem baru dibuat (semua data lama, kalau ada, hilang).\n");
    } else if (strcmp(cmd, "ls") == 0) {
        fs_list();
    } else if (starts_with(cmd, "cat ")) {
        const char* filename = cmd + 4;
        uint8_t buf[2048];
        uint32_t size;
        if (fs_read(filename, buf, sizeof(buf) - 1, &size)) {
            uint32_t to_print = size < sizeof(buf) - 1 ? size : sizeof(buf) - 1;
            buf[to_print] = '\0';
            print_string((char*)buf);
            print_string("\n");
        } else {
            print_string("File gak ketemu: ");
            print_string(filename);
            print_string("\n");
        }
    } else if (starts_with(cmd, "rm ")) {
        const char* filename = cmd + 3;
        if (fs_delete(filename)) {
            print_string("Dihapus: ");
            print_string(filename);
            print_string("\n");
        } else {
            print_string("File gak ketemu: ");
            print_string(filename);
            print_string("\n");
        }
    } else if (strcmp(cmd, "fstest") == 0) {
        const char* content = "Halo dari GabutOS! Data ini beneran nempel di disk (bukan cuma RAM).\n";
        if (fs_write("hello.txt", (const uint8_t*)content, strlen_local(content))) {
            print_string("Nulis hello.txt sukses.\n");
        } else {
            print_string("Gagal nulis (tabel file penuh atau belum di-format?)\n");
        }

        uint8_t buf[256];
        uint32_t size;
        if (fs_read("hello.txt", buf, sizeof(buf) - 1, &size)) {
            buf[size < sizeof(buf) - 1 ? size : sizeof(buf) - 1] = '\0';
            print_string("Baca balik dari disk: ");
            print_string((char*)buf);
        }
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
    print_string("GabutOS v1.0.0 - First Stable Release\n");
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

    shell();
}
