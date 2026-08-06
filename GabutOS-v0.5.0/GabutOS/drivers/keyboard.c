#include "keyboard.h"
#include "idt.h"
#include "io.h"

#define KB_BUFFER_SIZE 128

static char kb_buffer[KB_BUFFER_SIZE];
static volatile int kb_read = 0;
static volatile int kb_write = 0;

static const char scancode_to_ascii[] = {
    0, 27, '1','2','3','4','5','6','7','8','9','0','-','=','\b',
    '\t','q','w','e','r','t','y','u','i','o','p','[',']','\n',
    0,'a','s','d','f','g','h','j','k','l',';','\'','`', 0,
    '\\','z','x','c','v','b','n','m',',','.','/', 0,'*', 0,' '
};

static void kb_push(char c) {
    int next = (kb_write + 1) % KB_BUFFER_SIZE;
    if (next != kb_read) {
        kb_buffer[kb_write] = c;
        kb_write = next;
    }
}

static void keyboard_handler(struct registers* regs) {
    (void)regs;
    uint8_t scancode = inb(0x60);

    if (scancode & 0x80) return;

    if (scancode < sizeof(scancode_to_ascii)) {
        char c = scancode_to_ascii[scancode];
        if (c != 0) kb_push(c);
    }
}

void keyboard_install(void) {
    register_interrupt_handler(33, keyboard_handler);
}

char keyboard_getchar(void) {
    while (kb_read == kb_write) {
        asm volatile ("hlt");
    }
    char c = kb_buffer[kb_read];
    kb_read = (kb_read + 1) % KB_BUFFER_SIZE;
    return c;
}
