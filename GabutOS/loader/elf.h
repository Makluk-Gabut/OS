#ifndef ELF_H
#define ELF_H

#include <stdint.h>

int elf_load(const uint8_t* data, uint32_t size, uint32_t* out_entry, int ring);

#endif
