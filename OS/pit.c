#include "pit.h"
#include "idt.h"
#include "io.h"

#define PIT_CHANNEL0_DATA 0x40
#define PIT_COMMAND       0x43
#define PIT_BASE_FREQ     1193182u // frekuensi osilator internal PIT (Hz)

static volatile uint32_t tick_count = 0;
static uint32_t configured_hz = 100; // default 100Hz -> 1 tick = 10ms

static void pit_handler(struct registers* regs) {
    (void)regs;
    tick_count++;
}

void pit_set_frequency(uint32_t hz) {
    if (hz == 0) hz = 100;
    configured_hz = hz;

    uint32_t divisor = PIT_BASE_FREQ / hz;
    if (divisor > 0xFFFF) divisor = 0xFFFF; // divisor cuma 16-bit
    if (divisor == 0) divisor = 1;

    // 0x36 = channel 0, lobyte/hibyte, mode 3 (square wave), binary mode
    outb(PIT_COMMAND, 0x36);
    outb(PIT_CHANNEL0_DATA, (uint8_t)(divisor & 0xFF));
    outb(PIT_CHANNEL0_DATA, (uint8_t)((divisor >> 8) & 0xFF));
}

void pit_install(void) {
    register_interrupt_handler(32, pit_handler); // IRQ0 -> interrupt 32
    pit_set_frequency(100); // 100Hz = tick tiap 10ms
}

uint32_t pit_get_ticks(void) {
    return tick_count;
}

void sleep_ms(uint32_t ms) {
    // Konversi ms ke jumlah tick berdasarkan frekuensi yang aktif sekarang
    uint32_t ticks_to_wait = (ms * configured_hz) / 1000;
    if (ticks_to_wait == 0) ticks_to_wait = 1;

    uint32_t target = tick_count + ticks_to_wait;
    while (tick_count < target) {
        asm volatile ("hlt"); // tidur, nunggu interrupt (PIT atau lainnya) bangunin
    }
}
