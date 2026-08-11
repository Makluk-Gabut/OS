#include "elf.h"
#include "pmm.h"
#include "vmm.h"
#include "screen.h"

#define ELF_MAGIC0 0x7F
#define ELF_MAGIC1 'E'
#define ELF_MAGIC2 'L'
#define ELF_MAGIC3 'F'

#define ET_EXEC 2
#define EM_386  3
#define PT_LOAD 1

#define ELF_MIN_LOAD_VADDR 0x1000000u
#define ELF_MAX_TRACKED_PAGES 256

struct elf32_ehdr {
    uint8_t e_ident[16];
    uint16_t e_type;
    uint16_t e_machine;
    uint32_t e_version;
    uint32_t e_entry;
    uint32_t e_phoff;
    uint32_t e_shoff;
    uint32_t e_flags;
    uint16_t e_ehsize;
    uint16_t e_phentsize;
    uint16_t e_phnum;
    uint16_t e_shentsize;
    uint16_t e_shnum;
    uint16_t e_shstrndx;
} __attribute__((packed));

struct elf32_phdr {
    uint32_t p_type;
    uint32_t p_offset;
    uint32_t p_vaddr;
    uint32_t p_paddr;
    uint32_t p_filesz;
    uint32_t p_memsz;
    uint32_t p_flags;
    uint32_t p_align;
} __attribute__((packed));

int elf_load(const uint8_t* data, uint32_t size, uint32_t* out_entry, int ring, uint32_t* page_directory, uint32_t max_pages) {
    if (size < sizeof(struct elf32_ehdr)) {
        print_string("[elf] file terlalu kecil buat jadi ELF valid\n");
        return 0;
    }

    const struct elf32_ehdr* eh = (const struct elf32_ehdr*)data;

    if (eh->e_ident[0] != ELF_MAGIC0 || eh->e_ident[1] != ELF_MAGIC1 ||
        eh->e_ident[2] != ELF_MAGIC2 || eh->e_ident[3] != ELF_MAGIC3) {
        print_string("[elf] magic number salah, bukan file ELF\n");
        return 0;
    }

    if (eh->e_type != ET_EXEC) {
        print_string("[elf] bukan ET_EXEC (statik executable)\n");
        return 0;
    }

    if (eh->e_machine != EM_386) {
        print_string("[elf] bukan arsitektur EM_386 (x86 32-bit)\n");
        return 0;
    }

    if ((uint32_t)eh->e_phoff + (uint32_t)eh->e_phnum * eh->e_phentsize > size) {
        print_string("[elf] program header table di luar batas file\n");
        return 0;
    }

    uint32_t* target_pd = page_directory ? page_directory : vmm_kernel_directory();

    asm volatile ("cli");
    uint32_t* original_cr3 = vmm_current_directory();
    int switched = (target_pd != original_cr3);
    if (switched) vmm_activate(target_pd);

    int ok = 1;
    uint32_t pages_allocated = 0;
    uint32_t allocated_vaddrs[ELF_MAX_TRACKED_PAGES];

    for (uint16_t i = 0; i < eh->e_phnum; i++) {
        const struct elf32_phdr* ph = (const struct elf32_phdr*)
            (data + eh->e_phoff + i * eh->e_phentsize);

        if (ph->p_type != PT_LOAD) continue;

        if (ph->p_vaddr < ELF_MIN_LOAD_VADDR) {
            print_string("[elf] segmen minta alamat di bawah batas aman (bentrok area kernel/heap)\n");
            ok = 0;
            break;
        }

        if (ph->p_offset + ph->p_filesz > size) {
            print_string("[elf] segmen file offset di luar batas file\n");
            ok = 0;
            break;
        }

        uint32_t seg_start = ph->p_vaddr & 0xFFFFF000;
        uint32_t seg_end = (ph->p_vaddr + ph->p_memsz + 4095) & 0xFFFFF000;
        uint32_t page_flags = PAGE_PRESENT | PAGE_RW | (ring == 3 ? PAGE_USER : 0);

        for (uint32_t page_addr = seg_start; page_addr < seg_end; page_addr += 4096) {
            if (vmm_is_mapped_in(target_pd, page_addr)) continue;

            if (max_pages != 0 && pages_allocated >= max_pages) {
                print_string("[elf] program melebihi quota memori (");
                print_dec(max_pages);
                print_string(" halaman)\n");
                ok = 0;
                break;
            }

            uint32_t phys = pmm_alloc_page();
            if (phys == 0) {
                print_string("[elf] kehabisan RAM fisik pas load segmen\n");
                ok = 0;
                break;
            }
            if (!vmm_map_page_in(target_pd, page_addr, phys, page_flags)) {
                print_string("[elf] gagal map halaman segmen\n");
                ok = 0;
                break;
            }

            if (pages_allocated < ELF_MAX_TRACKED_PAGES) {
                allocated_vaddrs[pages_allocated] = page_addr;
            }
            pages_allocated++;
        }
        if (!ok) break;

        uint8_t* dest = (uint8_t*)ph->p_vaddr;
        const uint8_t* src = data + ph->p_offset;
        for (uint32_t k = 0; k < ph->p_filesz; k++) dest[k] = src[k];
        for (uint32_t k = ph->p_filesz; k < ph->p_memsz; k++) dest[k] = 0;
    }

    if (ok) {
        *out_entry = eh->e_entry;
    } else {
        uint32_t undo_count = pages_allocated < ELF_MAX_TRACKED_PAGES ? pages_allocated : ELF_MAX_TRACKED_PAGES;
        for (uint32_t k = 0; k < undo_count; k++) {
            vmm_unmap_and_free_in(target_pd, allocated_vaddrs[k]);
        }
    }

    if (switched) vmm_activate(original_cr3);
    asm volatile ("sti");

    return ok;
}
