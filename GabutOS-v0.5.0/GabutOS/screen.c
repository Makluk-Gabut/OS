#include "screen.h"
#include "serial.h"

static const size_t VGA_WIDTH = 80;
static const size_t VGA_HEIGHT = 25;
static uint16_t* vga_buffer = (uint16_t*) 0xB8000;
static size_t terminal_row = 0;
static size_t terminal_column = 0;
static uint8_t terminal_color = 0x0F;

void clear_screen(void) {
    for (size_t y = 0; y < VGA_HEIGHT; y++) {
        for (size_t x = 0; x < VGA_WIDTH; x++) {
            const size_t index = y * VGA_WIDTH + x;
            vga_buffer[index] = (uint16_t) ' ' | ((uint16_t) terminal_color << 8);
        }
    }
    terminal_row = 0;
    terminal_column = 0;
}

static void scroll(void) {
    for (size_t y = 1; y < VGA_HEIGHT; y++) {
        for (size_t x = 0; x < VGA_WIDTH; x++) {
            vga_buffer[(y - 1) * VGA_WIDTH + x] = vga_buffer[y * VGA_WIDTH + x];
        }
    }
    for (size_t x = 0; x < VGA_WIDTH; x++) {
        vga_buffer[(VGA_HEIGHT - 1) * VGA_WIDTH + x] = (uint16_t) ' ' | ((uint16_t) terminal_color << 8);
    }
    terminal_row = VGA_HEIGHT - 1;
}

void print_char(char c) {
    serial_putchar(c);

    if (c == '\n') {
        terminal_column = 0;
        terminal_row++;
    } else if (c == '\b') {
        if (terminal_column > 0) {
            terminal_column--;
            vga_buffer[terminal_row * VGA_WIDTH + terminal_column] = (uint16_t) ' ' | ((uint16_t) terminal_color << 8);
        }
    } else {
        const size_t index = terminal_row * VGA_WIDTH + terminal_column;
        vga_buffer[index] = (uint16_t) c | ((uint16_t) terminal_color << 8);
        terminal_column++;
        if (terminal_column >= VGA_WIDTH) {
            terminal_column = 0;
            terminal_row++;
        }
    }

    if (terminal_row >= VGA_HEIGHT) {
        scroll();
    }
}

void print_string(const char* str) {
    for (size_t i = 0; str[i] != '\0'; i++) print_char(str[i]);
}

void print_dec(uint32_t n) {
    if (n == 0) { print_char('0'); return; }
    char buf[10];
    int i = 0;
    while (n > 0) {
        buf[i++] = '0' + (n % 10);
        n /= 10;
    }
    while (i > 0) print_char(buf[--i]);
}

void print_hex(uint32_t n) {
    const char* hex = "0123456789ABCDEF";
    print_string("0x");
    for (int i = 28; i >= 0; i -= 4) {
        print_char(hex[(n >> i) & 0xF]);
    }
}
