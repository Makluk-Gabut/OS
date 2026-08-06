#include "pit.h"
#include "idt.h"
#include "io.h"

#define PIT_CHANNEL0_DATA 0x40
#define PIT_COMMAND       0x43
#define PIT_BASE_FREQ     1193182u

static volatile uint32_t tick_count = 0;
static uint32_t configured_hz = 100;

static void pit_handler(struct registers* regs) {
    (void)regs;
    tick_count++;
}

void pit_set_frequency(uint32_t hz) {
    if (hz == 0) hz = 100;
    configured_hz = hz;

    uint32_t divisor = PIT_BASE_FREQ / hz;
    if (divisor > 0xFFFF) divisor = 0xFFFF;
    if (divisor == 0) divisor = 1;

    outb(PIT_COMMAND, 0x36);
    outb(PIT_CHANNEL0_DATA, (uint8_t)(divisor & 0xFF));
    outb(PIT_CHANNEL0_DATA, (uint8_t)((divisor >> 8) & 0xFF));
}

void pit_install(void) {
    register_interrupt_handler(32, pit_handler);
    pit_set_frequency(100);
}

uint32_t pit_get_ticks(void) {
    return tick_count;
}

void sleep_ms(uint32_t ms) {

    uint32_t ticks_to_wait = (ms * configured_hz) / 1000;
    if (ticks_to_wait == 0) ticks_to_wait = 1;

    uint32_t target = tick_count + ticks_to_wait;
    while (tick_count < target) {
        asm volatile ("hlt");
    }
}
