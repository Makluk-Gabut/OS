#include "pmm.h"
#include "screen.h"

#define PAGE_SIZE 4096

#define MAX_SUPPORTED_MEM (256u * 1024 * 1024)
#define MAX_PAGES (MAX_SUPPORTED_MEM / PAGE_SIZE)
#define BITMAP_SIZE (MAX_PAGES / 8)

extern uint8_t kernel_start[];
extern uint8_t kernel_end[];

static uint8_t bitmap[BITMAP_SIZE];
static uint32_t total_pages = 0;
static uint32_t free_pages = 0;

static void bitmap_set(uint32_t page) {
    if (page >= MAX_PAGES) return;
    bitmap[page / 8] |= (uint8_t)(1 << (page % 8));
}

static void bitmap_clear(uint32_t page) {
    if (page >= MAX_PAGES) return;
    bitmap[page / 8] &= (uint8_t)~(1 << (page % 8));
}

static int bitmap_test(uint32_t page) {
    if (page >= MAX_PAGES) return 1;
    return bitmap[page / 8] & (1 << (page % 8));
}

static void reserve_page_if_free(uint32_t page) {
    if (!bitmap_test(page)) {
        bitmap_set(page);
        free_pages--;
    }
}

void pmm_init(struct multiboot_info* mbi) {

    for (uint32_t i = 0; i < BITMAP_SIZE; i++) bitmap[i] = 0xFF;
    free_pages = 0;

    if (mbi->flags & MULTIBOOT_INFO_MEM_MAP) {
        uint32_t mmap_addr = mbi->mmap_addr;
        uint32_t mmap_end = mmap_addr + mbi->mmap_length;

        while (mmap_addr < mmap_end) {
            struct multiboot_mmap_entry* entry = (struct multiboot_mmap_entry*)mmap_addr;

            if (entry->type == MULTIBOOT_MEMORY_AVAILABLE) {
                uint64_t start = entry->addr;
                uint64_t end = start + entry->len;
                if (end > MAX_SUPPORTED_MEM) end = MAX_SUPPORTED_MEM;

                for (uint64_t addr = start; addr + PAGE_SIZE <= end; addr += PAGE_SIZE) {
                    uint32_t page = (uint32_t)(addr / PAGE_SIZE);
                    if (bitmap_test(page)) {
                        bitmap_clear(page);
                        free_pages++;
                    }
                }
            }

            mmap_addr += entry->size + sizeof(entry->size);
        }
    } else if (mbi->flags & MULTIBOOT_INFO_MEM) {

        uint64_t total_bytes = (uint64_t)1024 * 1024 + (uint64_t)mbi->mem_upper * 1024;
        if (total_bytes > MAX_SUPPORTED_MEM) total_bytes = MAX_SUPPORTED_MEM;

        for (uint32_t addr = 0x100000; addr + PAGE_SIZE <= total_bytes; addr += PAGE_SIZE) {
            bitmap_clear(addr / PAGE_SIZE);
            free_pages++;
        }
    }

    total_pages = free_pages;

    for (uint32_t page = 0; page < 256; page++) reserve_page_if_free(page);

    uint32_t kstart_page = (uint32_t)kernel_start / PAGE_SIZE;
    uint32_t kend_page = ((uint32_t)kernel_end + PAGE_SIZE - 1) / PAGE_SIZE;
    for (uint32_t page = kstart_page; page < kend_page; page++) reserve_page_if_free(page);
}

uint32_t pmm_alloc_page(void) {
    for (uint32_t page = 0; page < MAX_PAGES; page++) {
        if (!bitmap_test(page)) {
            bitmap_set(page);
            free_pages--;
            return page * PAGE_SIZE;
        }
    }
    return 0;
}

void pmm_free_page(uint32_t phys_addr) {
    uint32_t page = phys_addr / PAGE_SIZE;
    if (bitmap_test(page)) {
        bitmap_clear(page);
        free_pages++;
    }
}

uint32_t pmm_free_page_count(void) { return free_pages; }
uint32_t pmm_total_page_count(void) { return total_pages; }
