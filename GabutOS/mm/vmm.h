#ifndef VMM_H
#define VMM_H

#include <stdint.h>

#define PAGE_PRESENT 0x1
#define PAGE_RW      0x2
#define PAGE_USER    0x4

void vmm_init(void);

uint32_t vmm_map_page(uint32_t virtual_addr, uint32_t physical_addr, uint32_t flags);
int vmm_is_mapped(uint32_t virtual_addr);

uint32_t vmm_map_page_in(uint32_t* pd, uint32_t virtual_addr, uint32_t physical_addr, uint32_t flags);
int vmm_is_mapped_in(uint32_t* pd, uint32_t virtual_addr);
void vmm_unmap_and_free_in(uint32_t* pd, uint32_t virtual_addr);

uint32_t* vmm_create_address_space(void);
void vmm_destroy_address_space(uint32_t* pd);
uint32_t* vmm_kernel_directory(void);
uint32_t* vmm_current_directory(void);
void vmm_activate(uint32_t* pd);

#endif
