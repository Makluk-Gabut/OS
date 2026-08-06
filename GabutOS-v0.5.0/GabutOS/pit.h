#ifndef PIT_H
#define PIT_H

#include <stdint.h>

void pit_install(void);
void pit_set_frequency(uint32_t hz);
uint32_t pit_get_ticks(void);
void sleep_ms(uint32_t ms);

#endif
