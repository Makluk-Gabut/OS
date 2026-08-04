# Nama hasil akhir sistem operasi kamu
TARGET = gabutos.bin

# Kompiler dan alat yang digunakan
CC = i686-elf-gcc
AS = nasm
LD = i686-elf-ld

# Bendera Kompiler C
CFLAGS = -std=gnu99 -ffreestanding -O2 -Wall -Wextra

# Semua objek yang perlu di-link
C_SOURCES = kernel.c screen.c serial.c gdt.c idt.c interrupts.c keyboard.c pit.c pmm.c vmm.c heap.c
ASM_SOURCES = assembler.asm gdt_flush.asm idt_flush.asm isr.asm

C_OBJECTS = $(C_SOURCES:.c=.o)
ASM_OBJECTS = $(ASM_SOURCES:.asm=.o)
OBJECTS = $(ASM_OBJECTS) $(C_OBJECTS)

all: $(TARGET)

%.o: %.asm
	$(AS) -f elf32 $< -o $@

%.o: %.c
	$(CC) -c $< -o $@ $(CFLAGS)

$(TARGET): $(OBJECTS)
	$(CC) -T linker.ld -o $(TARGET) -ffreestanding -O2 -nostdlib $(OBJECTS) -lgcc

clean:
	rm -f *.o $(TARGET)

run: $(TARGET)
	qemu-system-i386 -kernel $(TARGET)
