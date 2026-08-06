#ifndef HEAP_H
#define HEAP_H

#include <stddef.h>
#include <stdint.h>

void heap_init(void);
void* kmalloc(size_t size);
void kfree(void* ptr);

size_t heap_used_bytes(void);
size_t heap_free_bytes(void);
size_t heap_mapped_bytes(void);

#endif
