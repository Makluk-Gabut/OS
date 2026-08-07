#ifndef USERMODE_H
#define USERMODE_H

#include <stdint.h>

void usermode_install(void);
void usermode_run_demo(void);
void usermode_jump(uint32_t entry_eip);

#endif
