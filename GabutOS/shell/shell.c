#include "shell.h"
#include "screen.h"
#include "keyboard.h"
#include "pit.h"
#include "pmm.h"
#include "heap.h"
#include "fs.h"
#include "elf.h"
#include "vmm.h"
#include "usermode.h"
#include "task.h"
#include "scheduler.h"
#include "test_program.h"
#include "demo_task_a.h"
#include "demo_task_b.h"
#include "demo_task_c.h"
#include "demo_task_crash.h"
#include "demo_iso_x.h"
#include "demo_iso_y.h"
#include "demo_crash_iso.h"
#include "demo_hog.h"
#include <stdbool.h>
#include <stdint.h>

#define RUN_TASK_MAX_PAGES 64

int strcmp(const char* s1, const char* s2) {
    while (*s1 && (*s1 == *s2)) { s1++; s2++; }
    return *(const unsigned char*)s1 - *(const unsigned char*)s2;
}

static uint32_t strlen_local(const char* s) {
    uint32_t n = 0;
    while (s[n] != '\0') n++;
    return n;
}

void shell_tokenize(char* line, struct shell_command* out) {
    out->argc = 0;

    char* p = line;
    while (*p != '\0' && out->argc < SHELL_MAX_ARGS) {
        while (*p == ' ') p++;
        if (*p == '\0') break;

        out->argv[out->argc] = p;
        out->argc++;

        while (*p != ' ' && *p != '\0') p++;
        if (*p == ' ') {
            *p = '\0';
            p++;
        }
    }
}

static void cmd_help(struct shell_command* c) {
    (void)c;
    print_string("Available commands: help, clear, mem, uptime, pages, alloctest, usermode, fsformat, ls, cat <nama>, rm <nama>, fstest, loadtest, run <nama>, multitask, ps, crashtest, isotest\n");
}

static void cmd_clear(struct shell_command* c) {
    (void)c;
    clear_screen();
}

static void cmd_pages(struct shell_command* c) {
    (void)c;
    print_string("Physical pages: ");
    print_dec(pmm_free_page_count());
    print_string(" free / ");
    print_dec(pmm_total_page_count());
    print_string(" total (");
    print_dec(pmm_free_page_count() * 4);
    print_string(" KB bebas)\n");
}

static void cmd_uptime(struct shell_command* c) {
    (void)c;
    uint32_t ticks = pit_get_ticks();
    print_string("Ticks: ");
    print_dec(ticks);
    print_string(" (~");
    print_dec(ticks / 100);
    print_string(" detik sejak boot)\n");
}

static void cmd_mem(struct shell_command* c) {
    (void)c;
    print_string("Heap: dipakai=");
    print_dec((uint32_t)heap_used_bytes());
    print_string("B  bebas=");
    print_dec((uint32_t)heap_free_bytes());
    print_string("B  ter-mapping=");
    print_dec((uint32_t)heap_mapped_bytes());
    print_string("B\n");
}

static void cmd_alloctest(struct shell_command* c) {
    (void)c;
    print_string("Alokasi A(64B) B(128B) C(32B)...\n");
    void* a = kmalloc(64);
    void* b = kmalloc(128);
    void* cc = kmalloc(32);
    print_string("  A="); print_hex((uint32_t)a);
    print_string(" B="); print_hex((uint32_t)b);
    print_string(" C="); print_hex((uint32_t)cc);
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
    kfree(cc);
    kfree(d);
    print_string("Semua di-free lagi.\n");
}

static void cmd_usermode(struct shell_command* c) {
    (void)c;
    usermode_run_demo();
}

static void cmd_fsformat(struct shell_command* c) {
    (void)c;
    fs_format();
    print_string("Filesystem baru dibuat (semua data lama, kalau ada, hilang).\n");
}

static void cmd_ls(struct shell_command* c) {
    (void)c;
    fs_list();
}

static void cmd_cat(struct shell_command* c) {
    if (c->argc < 2) {
        print_string("Pakai: cat <nama>\n");
        return;
    }
    const char* filename = c->argv[1];
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
}

static void cmd_rm(struct shell_command* c) {
    if (c->argc < 2) {
        print_string("Pakai: rm <nama>\n");
        return;
    }
    const char* filename = c->argv[1];
    if (fs_delete(filename)) {
        print_string("Dihapus: ");
        print_string(filename);
        print_string("\n");
    } else {
        print_string("File gak ketemu: ");
        print_string(filename);
        print_string("\n");
    }
}

static void cmd_fstest(struct shell_command* c) {
    (void)c;
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
}

static void cmd_loadtest(struct shell_command* c) {
    (void)c;
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
}

static void cmd_run(struct shell_command* c) {
    if (c->argc < 2) {
        print_string("Pakai: run <nama>\n");
        return;
    }
    const char* filename = c->argv[1];

    if (!scheduler_is_active()) {
        print_string("Jalankan 'multitask' dulu (nyalain scheduler), baru 'run <nama>' bisa dipakai.\n");
        return;
    }

    uint32_t buf_cap = 65536;
    uint8_t* buf = (uint8_t*)kmalloc(buf_cap);
    if (!buf) {
        print_string("Gagal alokasi buffer buat load file.\n");
        return;
    }

    uint32_t size;
    if (!fs_read(filename, buf, buf_cap, &size)) {
        print_string("File gak ketemu: ");
        print_string(filename);
        print_string("\n");
        kfree(buf);
        return;
    }

    if (size > buf_cap) {
        print_string("[warn] file lebih besar dari buffer loader (64KB), kepotong.\n");
        size = buf_cap;
    }

    uint32_t* pd = vmm_create_address_space();
    if (!pd) {
        print_string("Gagal bikin address space (kehabisan RAM fisik?)\n");
        kfree(buf);
        return;
    }

    uint32_t entry;
    if (!elf_load(buf, size, &entry, 3, pd, RUN_TASK_MAX_PAGES)) {
        print_string("[elf] gagal load (lihat pesan error di atas)\n");
        vmm_destroy_address_space(pd);
        kfree(buf);
        return;
    }

    struct task* t = task_create(filename, entry, 3, 1, pd);
    if (!t) {
        print_string("Gagal bikin task (kehabisan heap?)\n");
        vmm_destroy_address_space(pd);
        kfree(buf);
        return;
    }

    scheduler_add_task(t);
    print_string("[elf] '");
    print_string(filename);
    print_string("' jalan sebagai task terisolasi (page directory sendiri).\n");
    kfree(buf);
}

static void cmd_multitask(struct shell_command* c) {
    (void)c;
    if (scheduler_is_active()) {
        print_string("Scheduler udah jalan. Reboot buat coba dari awal.\n");
        return;
    }

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

static void cmd_ps(struct shell_command* c) {
    (void)c;
    if (!scheduler_is_active()) {
        print_string("Scheduler belum aktif. Jalankan 'multitask' dulu.\n");
    } else {
        scheduler_list();
    }
}

static void cmd_crashtest(struct shell_command* c) {
    (void)c;
    if (!scheduler_is_active()) {
        print_string("Jalankan 'multitask' dulu, baru 'crashtest' buat lihat task lain tetap hidup.\n");
        return;
    }
    uint32_t entry_crash;
    if (elf_load(demo_task_crash_elf, demo_task_crash_elf_size, &entry_crash, 3, NULL, 0)) {
        struct task* bad_task = task_create("task-CRASH(r3)", entry_crash, 3, 1, NULL);
        scheduler_add_task(bad_task);
        print_string("task-CRASH ditambahkan -- dia bakal nulis ke alamat gak valid dan crash.\n");
        print_string("Perhatiin: task lain (A/B/C) harusnya tetap jalan normal setelahnya.\n");
    }
}

static void cmd_isotest(struct shell_command* c) {
    (void)c;
    if (!scheduler_is_active()) {
        print_string("Jalankan 'multitask' dulu buat nyalain scheduler-nya.\n");
        return;
    }

    uint32_t* pd_x = vmm_create_address_space();
    uint32_t* pd_y = vmm_create_address_space();

    if (!pd_x || !pd_y) {
        print_string("Gagal bikin address space baru (kehabisan RAM fisik?)\n");
        return;
    }

    uint32_t entry_x, entry_y;
    int ok_x = elf_load(demo_iso_x_elf, demo_iso_x_elf_size, &entry_x, 3, pd_x, 0);
    int ok_y = elf_load(demo_iso_y_elf, demo_iso_y_elf_size, &entry_y, 3, pd_y, 0);

    if (!ok_x || !ok_y) return;

    struct task* task_x = task_create("task-X(iso)", entry_x, 3, 1, pd_x);
    struct task* task_y = task_create("task-Y(iso)", entry_y, 3, 1, pd_y);

    if (!task_x || !task_y) {
        print_string("Gagal bikin task (kehabisan heap?)\n");
        return;
    }

    scheduler_add_task(task_x);
    scheduler_add_task(task_y);

    print_string("task-X dan task-Y ditambahkan, DUA-DUANYA di-load ke alamat\n");
    print_string("virtual yang SAMA (0x1000000), tapi di page directory TERPISAH.\n");
    print_string("Kalau isolasi beneran jalan: X terus ngeprint 'X', Y terus ngeprint 'Y',\n");
    print_string("gak ada yang ke-corrupt walau alamatnya identik.\n");
}

struct shell_command_entry {
    const char* name;
    void (*handler)(struct shell_command*);
};

static const struct shell_command_entry commands[] = {
    {"help", cmd_help},
    {"clear", cmd_clear},
    {"pages", cmd_pages},
    {"uptime", cmd_uptime},
    {"mem", cmd_mem},
    {"alloctest", cmd_alloctest},
    {"usermode", cmd_usermode},
    {"fsformat", cmd_fsformat},
    {"ls", cmd_ls},
    {"cat", cmd_cat},
    {"rm", cmd_rm},
    {"fstest", cmd_fstest},
    {"loadtest", cmd_loadtest},
    {"run", cmd_run},
    {"multitask", cmd_multitask},
    {"ps", cmd_ps},
    {"crashtest", cmd_crashtest},
    {"isotest", cmd_isotest},
};

#define NUM_COMMANDS (sizeof(commands) / sizeof(commands[0]))

static void dispatch(struct shell_command* c) {
    if (c->argc == 0) return;

    for (uint32_t i = 0; i < NUM_COMMANDS; i++) {
        if (strcmp(c->argv[0], commands[i].name) == 0) {
            commands[i].handler(c);
            return;
        }
    }

    print_string("Command not found: ");
    print_string(c->argv[0]);
    print_string("\n");
}

void shell_run(void) {
    char line[SHELL_LINE_LEN];
    int i = 0;

    print_string("GabutOS> ");

    while (true) {
        char ch = keyboard_getchar();

        if (ch == '\n') {
            print_char('\n');
            line[i] = '\0';

            struct shell_command cmd;
            shell_tokenize(line, &cmd);
            dispatch(&cmd);

            i = 0;
            print_string("GabutOS> ");
        } else if (ch == '\b') {
            if (i > 0) {
                i--;
                print_char('\b');
            }
        } else if (ch != 0 && i < SHELL_LINE_LEN - 1) {
            line[i++] = ch;
            print_char(ch);
        }
    }
}
