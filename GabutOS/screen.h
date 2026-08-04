#ifndef SCREEN_H
#define SCREEN_H

#include <stddef.h>
#include <stdint.h>

void clear_screen(void);
void print_char(char c);
void print_string(const char* str);
void print_dec(uint32_t n);
void print_hex(uint32_t n);

#endif
