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

int elf_load(const uint8_t* data, uint32_t size, uint32_t* out_entry) {
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

    for (uint16_t i = 0; i < eh->e_phnum; i++) {
        const struct elf32_phdr* ph = (const struct elf32_phdr*)
            (data + eh->e_phoff + i * eh->e_phentsize);

        if (ph->p_type != PT_LOAD) continue;

        if (ph->p_vaddr < ELF_MIN_LOAD_VADDR) {
            print_string("[elf] segmen minta alamat di bawah batas aman (bentrok area kernel/heap)\n");
            return 0;
        }

        if (ph->p_offset + ph->p_filesz > size) {
            print_string("[elf] segmen file offset di luar batas file\n");
            return 0;
        }

        uint32_t seg_start = ph->p_vaddr & 0xFFFFF000;
        uint32_t seg_end = (ph->p_vaddr + ph->p_memsz + 4095) & 0xFFFFF000;

        for (uint32_t page_addr = seg_start; page_addr < seg_end; page_addr += 4096) {
            if (vmm_is_mapped(page_addr)) continue;

            uint32_t phys = pmm_alloc_page();
            if (phys == 0) {
                print_string("[elf] kehabisan RAM fisik pas load segmen\n");
                return 0;
            }
            if (!vmm_map_page(page_addr, phys, PAGE_PRESENT | PAGE_RW | PAGE_USER)) {
                print_string("[elf] gagal map halaman segmen\n");
                return 0;
            }
        }

        uint8_t* dest = (uint8_t*)ph->p_vaddr;
        const uint8_t* src = data + ph->p_offset;
        for (uint32_t k = 0; k < ph->p_filesz; k++) dest[k] = src[k];
        for (uint32_t k = ph->p_filesz; k < ph->p_memsz; k++) dest[k] = 0;
    }

    *out_entry = eh->e_entry;
    return 1;
}
