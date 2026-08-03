#ifndef PMM_H
#define PMM_H

#include <stdint.h>
#include "multiboot.h"

void pmm_init(struct multiboot_info* mbi);
uint32_t pmm_alloc_page(void);   // return physical address, 0 kalau kehabisan
void pmm_free_page(uint32_t phys_addr);
uint32_t pmm_free_page_count(void);
uint32_t pmm_total_page_count(void);

#endif
