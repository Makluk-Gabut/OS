#include "heap.h"
#include "pmm.h"
#include "vmm.h"

#define HEAP_START 0x400000u
#define HEAP_MAX   0x800000u

typedef struct block_header {
    size_t size;
    int free;
    struct block_header* next;
} block_header_t;

#define HEADER_SIZE sizeof(block_header_t)
#define ALIGN8(x) (((x) + 7u) & ~7u)

static block_header_t* first_block = NULL;
static uint32_t heap_break = HEAP_START;
static uint32_t heap_mapped_end = HEAP_START;

void heap_init(void) {
    first_block = NULL;
    heap_break = HEAP_START;
    heap_mapped_end = HEAP_START;
}

static int heap_ensure_mapped(uint32_t extra_bytes) {
    uint32_t needed_end = heap_break + extra_bytes;

    while (heap_mapped_end < needed_end) {
        if (heap_mapped_end >= HEAP_MAX) return 0;

        uint32_t phys = pmm_alloc_page();
        if (phys == 0) return 0;

        if (!vmm_map_page(heap_mapped_end, phys, PAGE_PRESENT | PAGE_RW)) {
            pmm_free_page(phys);
            return 0;
        }

        heap_mapped_end += 4096;
    }
    return 1;
}

void* kmalloc(size_t size) {
    if (size == 0) return NULL;
    size = ALIGN8(size);

    block_header_t* cur = first_block;
    while (cur) {
        if (cur->free && cur->size >= size) {

            if (cur->size >= size + HEADER_SIZE + 8) {
                block_header_t* remainder = (block_header_t*)((uint8_t*)cur + HEADER_SIZE + size);
                remainder->size = cur->size - size - HEADER_SIZE;
                remainder->free = 1;
                remainder->next = cur->next;

                cur->next = remainder;
                cur->size = size;
            }
            cur->free = 0;
            return (void*)((uint8_t*)cur + HEADER_SIZE);
        }
        cur = cur->next;
    }

    block_header_t* last = NULL;
    cur = first_block;
    while (cur) { last = cur; cur = cur->next; }

    uint32_t block_total = HEADER_SIZE + size;
    if (!heap_ensure_mapped(block_total)) return NULL;

    block_header_t* block = (block_header_t*)heap_break;
    block->size = size;
    block->free = 0;
    block->next = NULL;

    if (last) last->next = block;
    else first_block = block;

    heap_break += block_total;

    return (void*)((uint8_t*)block + HEADER_SIZE);
}

void kfree(void* ptr) {
    if (!ptr) return;

    block_header_t* block = (block_header_t*)((uint8_t*)ptr - HEADER_SIZE);
    block->free = 1;

    while (block->next && block->next->free &&
           (uint8_t*)block->next == (uint8_t*)block + HEADER_SIZE + block->size) {
        block->size += HEADER_SIZE + block->next->size;
        block->next = block->next->next;
    }
}

size_t heap_used_bytes(void) {
    size_t total = 0;
    for (block_header_t* b = first_block; b; b = b->next) {
        if (!b->free) total += b->size;
    }
    return total;
}

size_t heap_free_bytes(void) {
    size_t total = 0;
    for (block_header_t* b = first_block; b; b = b->next) {
        if (b->free) total += b->size;
    }
    return total;
}

size_t heap_mapped_bytes(void) {
    return heap_mapped_end - HEAP_START;
}
