TARGET = gabutos.bin

CC = i686-elf-gcc
AS = nasm
LD = i686-elf-ld

INCLUDES = -Icpu -Imm -Idrivers -Ifs -Iloader -Itask
CFLAGS = -std=gnu99 -ffreestanding -O2 -Wall -Wextra $(INCLUDES)

C_SOURCES = kernel.c \
            cpu/gdt.c cpu/idt.c cpu/interrupts.c cpu/tss.c cpu/usermode.c \
            mm/pmm.c mm/vmm.c mm/heap.c \
            drivers/screen.c drivers/serial.c drivers/keyboard.c drivers/pit.c drivers/ata.c \
            fs/fs.c \
            loader/elf.c \
            task/task.c task/scheduler.c

ASM_SOURCES = boot/assembler.asm \
              cpu/gdt_flush.asm cpu/idt_flush.asm cpu/isr.asm cpu/usermode_asm.asm

C_OBJECTS = $(C_SOURCES:.c=.o)
ASM_OBJECTS = $(ASM_SOURCES:.asm=.o)
OBJECTS = $(ASM_OBJECTS) $(C_OBJECTS)

all: $(TARGET)

%.o: %.asm
	$(AS) -f elf32 $< -o $@

%.o: %.c
	$(CC) -c $< -o $@ $(CFLAGS)

$(TARGET): $(OBJECTS)
	$(CC) -T boot/linker.ld -o $(TARGET) -ffreestanding -O2 -nostdlib $(OBJECTS) -lgcc

clean:
	rm -f $(OBJECTS) $(TARGET)

run: $(TARGET)
	qemu-system-i386 -kernel $(TARGET)
