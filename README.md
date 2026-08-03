# Gabut OS

**Gabut OS** is a 32-bit x86 hobby operating system developed from scratch. This project implements fundamental operating system kernel concepts including memory management, interrupt handling, and basic hardware drivers.

---

## 🚀 Key Features & Components

The kernel is built with a modular architecture consisting of the following core components:

* **Kernel Core (`kernel.c`)**: The main kernel entry point that initializes all subsystems.
* **Bootloader Compliance (`multiboot.h`, `linker.ld`)**: Complies with the Multiboot specification to support booting via GRUB or emulators (QEMU/Bochs).
* **Global Descriptor Table / GDT (`gdt.c`, `gdt.h`, `gdt_flush.asm`)**: Configures x86 memory protection and segmentation.
* **Interrupt System (`idt.c`, `idt.h`, `interrupts.c`, `isr.asm`, `idt_flush.asm`)**:
  * **Interrupt Descriptor Table (IDT)** for handling hardware and software interrupt routines.
  * **Interrupt Service Routines (ISR)** & **IRQ (Interrupt Request)** handling.
* **Memory Management**:
  * **Physical Memory Manager / PMM (`pmm.c`, `pmm.h`)**: Manages physical page frame allocation.
  * **Virtual Memory Manager / VMM (`vmm.c`, `vmm.h`)**: Implements x86 paging for virtual-to-physical memory mapping.
* **Drivers & Hardware I/O**:
  * **Screen Driver (`screen.c`, `screen.h`)**: Handles text output to the VGA text mode buffer (80x25).
  * **Keyboard Driver (`keyboard.c`, `keyboard.h`)**: Handles input from PS/2 keyboards.
  * **Programmable Interval Timer / PIT (`pit.c`, `pit.h`)**: Manages hardware system ticks and timing.
  * **Port I/O Helper (`io.h`)**: Inline assembly helpers for hardware I/O port communication (`inb`, `outb`).

---

## 📂 Project Directory Structure

```text
OS/
├── assembler.asm             # Initial assembly entry point (bootloader wrapper)
├── gdt.c / gdt.h             # GDT setup and management
├── gdt_flush.asm             # Reloads GDT segment registers
├── idt.c / idt.h             # IDT initialization and gate setup
├── idt_flush.asm             # Loads IDTR register
├── interrupts.c              # C-level ISR & IRQ handlers
├── isr.asm                   # Low-level assembly ISR/IRQ wrappers
├── io.h                      # Low-level port I/O routines
├── kernel.c                  # Main kernel entry point
├── keyboard.c / keyboard.h   # PS/2 keyboard driver
├── linker.ld                 # Linker script for kernel memory layout
├── Makefile                  # Main build script
├── Makefile.tested-with-gcc-m32 # Build configuration tested with 32-bit GCC
├── multiboot.h               # Multiboot header definitions
├── pit.c / pit.h             # Programmable Interval Timer driver
├── pmm.c / pmm.h             # Physical Memory Manager
├── screen.c / screen.h       # VGA text mode screen driver
├── status.md                 # Development status and progress notes
└── vmm.c / vmm.h             # Virtual Memory Manager (Paging)
