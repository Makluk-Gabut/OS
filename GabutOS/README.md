# GabutOS

A custom **operating system** built from scratch out of pure boredom.

This isn't Windows, Linux, or macOS. It's just my low-level sandbox experiment to learn how a computer actually works from initial boot to displaying text on the screen.

### File Structure
```
GabutOS/
├── boot/
│   ├── assembler.asm               # Entry point, Multiboot header, stack setup
│   └── linker.ld                   # Linker script
├── cpu/
│   ├── gdt.c / gdt.h               # Global Descriptor Table
│   ├── gdt_flush.asm               # Flush segment registers
│   ├── idt.c / idt.h               # Interrupt Descriptor Table & PIC remap
│   ├── idt_flush.asm               # Load IDT pointer
│   ├── isr.asm                     # Interrupt Service Routine entry points
│   ├── interrupts.c                # Exception handling & IRQ handlers
│   ├── tss.c / tss.h               # Task State Segment (Userland support)
│   ├── usermode.c / usermode.h     # Context switch code & userland helpers
│   ├── usermode_asm.asm            # Low-level usermode switch routine
│   └── io.h                        # Low-level I/O port helper (inb/outb)
├── drivers/
│   ├── screen.c / screen.h         # Driver VGA text mode + serial mirror
│   ├── keyboard.c / keyboard.h     # Driver keyboard IRQ-driven
│   ├── serial.c / serial.h         # Driver serial port COM1
│   ├── ata.c / ata.h               # Driver ATA PIO disk
│   └── pit.c / pit.h               # Programmable Interval Timer (preemption tick)
├── fs/
│   └── fs.c / fs.h                 # Flat filesystem implementation
├── loader/
│   ├── elf.c / elf.h               # ELF binary loader
│   ├── test_program.h              # Binary header test program
│   ├── demo_task_a.h               # Demo userland task A
│   ├── demo_task_b.h               # Demo userland task B
│   ├── demo_task_c.h               # Demo userland task C
│   ├── demo_task_crash.h          # Demo crash test task
│   ├── demo_hog.h                  # Demo CPU hogging task
│   ├── demo_iso_x.h                # Demo isolated process X
│   ├── demo_iso_y.h                # Demo isolated process Y
│   └── demo_crash_iso.h            # Demo isolated process crash test
├── mm/
│   ├── multiboot.h                 # Multiboot structure definition
│   ├── pmm.c / pmm.h               # Physical Memory Manager (page frame allocator)
│   ├── vmm.c / vmm.h               # Virtual Memory Manager & paging
│   └── heap.c / heap.h             # Dynamic heap allocator (kmalloc/kfree)
├── shell/
│   └── shell.c / shell.h           # Interactive shell & command parser (argc/argv)
├── task/
│   ├── task.c / task.h             # Process management & control block (PCB)
│   └── scheduler.c / scheduler.h   # Preemptive Round-Robin scheduler
├── kernel.c                        # Kernel entry point & core initialization
├── README.md                       # Project development status & notes
├── Makefile                        # Build automation script
├── Makefile.tested-with-gcc-m32    # Makefile alternatif untuk gcc -m32
└── .gitignore                      # Git ignore file, wait i dont have gitignore

```

### Current Status
**v1.1.0**: Still ongoing (whenever I get free time).

* **v1.1.0:** Refactored `kernel.c`. All command handlers moved to `shell/shell.c`. The shell parser now uses a proper tokenizer for `argc`/`argv` (no more manual `strcmp` or `starts_with`).
* **v1.0.0 (First Stable Release):** All initial roadmap items completed. The filesystem now truly supports deleting files + reclaiming disk space, plus per-task memory quotas. Remaining limitations are honestly documented in code, not hidden.
* **v0.9.0:** Auto-reclaiming code/data pages & page directory of killed/terminated isolated tasks. The `run` command now automatically executes tasks in isolation mode.
* **v0.8.0:** Multitasking hardening: fair preemption via PIT ticks (~10ms), killing crashed tasks without halting the entire kernel, isolated page directories per task, and stack page reclamation.
* **v0.7.0:** Multitasking (round-robin, mixed Ring 0 + Ring 3, sleep).
* **v0.6.0:** ELF loader.
* **v0.5.0:** ATA PIO disk driver + simple flat filesystem.
* **v0.4.0:** Ring 0 -> Ring 3 transition (usermode + syscalls).
* **v0.3.1:** Serial port driver (COM1, polling, 38400 8N1). All `print_string`/`print_dec`/`print_hex` outputs are automatically mirrored to serial via `screen.c`. Debugging is as simple as running `qemu ... -serial file:log.txt` then `cat log.txt`. No manual VGA screendumps needed anymore.
* **v0.3.0:** Dynamic memory heap (`kmalloc`/`kfree`, free-list, first-fit + split + merge-forward). Automatically extended via PMM+VMM using `vmm_map_page()`.
* **v0.2.0:** Paging / virtual memory.

### Current Features & Working Parts
* Bootable via GRUB / Multiboot
* Installed GDT (flat memory model, Ring 3 slots ready)
* IDT + PIC remapping, CPU exceptions (0-31) & IRQs (32-47) handled properly
* Keyboard IRQ-driven: CPU executes `hlt` when idle instead of 100% busy-looping
* VGA text mode output & auto-scroll
* Serial port logging (COM1)
* Flat filesystem + ATA PIO driver (Create, Read, Delete, Space Reclamation)
* Multitasking (Round-Robin, Ring 0 & Ring 3, per-task memory isolation, task crash recovery)
* Interactive shell (`help`, `clear`, `mem`, `run`, `alloctest`, etc.) with `argc`/`argv` parser

### Known Limitations (Documented in Code)
* `kfree()` only merges forward, not backward: requires a doubly-linked list for backward merging.
* `vmm_map_page()` triggers an explicit PANIC if it needs a new page table at a physical address >4MB (outside the identity-map) due to missing temporary mapping. Highly unlikely at the current stage.
* Preemption runs automatically on PIT ticks (~10ms). The only remaining edge case is a Ring 0 task executing an intentional `cli` + infinite loop (a fundamental kernel trust boundary, not a scheduler bug).

### How to Build & Test (For the Brave)
```bash
cd OS
make
make run   # requires qemu-system-i386
```
*(Requires nasm, i686-elf-gcc cross-compiler, qemu, make, etc. Setup is a bit tedious, not recommended for beginners)*

To view serial debug logs:
```bash
qemu-system-i386 -kernel kernel.bin -serial file:log.txt
cat log.txt
```

### Roadmap Progress

1. ~~Paging / virtual memory~~ ✅ (v0.2.0)
2. ~~Heap proper (`kmalloc`/`kfree`)~~ ✅ (v0.3.0)
3. ~~Serial driver~~ ✅ (v0.3.1)
4. ~~Ring 0 → Ring 3 (usermode + syscall)~~ ✅ (v0.4.0)
5. ~~Disk driver (ATA PIO) + simple flat filesystem~~ ✅ (v0.5.0)
6. ~~ELF loader~~ ✅ (v0.6.0)
7. ~~Multitasking (round-robin, mixed ring0+ring3, sleep)~~ ✅ (v0.7.0)
8. ~~Safely kill crashed tasks without halting the kernel~~ ✅ (v0.8.0)
9. ~~Fair preemption~~ ✅ (v0.8.0)
10. ~~Real process isolation: separate page directory per task~~ ✅ (v0.8.0)
11. ~~Reclaim physical stack page of killed task~~ ✅ (v0.8.0)
12. ~~Reclaim code/data pages + page directory of killed isolated task~~ ✅ (v0.9.0)
13. ~~Automatic isolation for tasks executed via standard `run` command~~ ✅ (v0.9.0)
14. ~~Filesystem deletion + actual disk space reclamation~~ ✅ (v1.0.0)
15. ~~Per-task memory quota~~ ✅ (v1.0.0)

### Next Roadmap (Post-1.0.0)
Next big goal: **self-hosted C compiler + an interactive shell beyond fixed commands**, so anyone can actually write and compile C programs *inside* GabutOS itself: not just run ELFs built externally.

1. ~~Shell parser argc/argv~~ ✅ (v1.1.0)
2. Minimal line-based text editor: to write code directly inside GabutOS
3. Small C subset compiler first: not full C, just enough to recompile some demo programs
4. Minimal assembler + linker: allowing the compiler to produce valid ELFs runnable by the loader

If you're also bored and like messing around with low-level stuff, feel free to fork & join the fun 😂

Made with ❤️ + coffee + insomnia  
by Makluk Gabut
