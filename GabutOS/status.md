# GabutOS

An **operating system** project built from scratch out of pure boredom.

This isn't Windows, it isn't Linux, and it definitely isn't macOS.
It's just my low-level experiment to learn how a computer actually boots up, executes code, and prints stuff on the screen.

### File Structure
- `assembler.asm` → entry point, multiboot header, stack setup
- `kernel.c` → main orchestrator + shell
- `screen.c/.h` → VGA text mode driver
- `gdt.c/.h` + `gdt_flush.asm` → Global Descriptor Table
- `idt.c/.h` + `idt_flush.asm` + `isr.asm` + `isr.c` → Interrupt Descriptor Table, PIC remapping, exception & IRQ handlers
- `keyboard.c/.h` → IRQ-driven keyboard driver (no more polling)
- `io.h` → low-level `inb`/`outb` helpers
- `linker.ld` → linker script

### Current Status
**v0.3.1** — still ongoing (whenever I get some free time)

**Update:** Serial port driver (COM1, polling, 38400 8N1). All outputs from `print_string`/`print_dec`/`print_hex` are now automatically mirrored to serial as well (via `print_char` in `screen.c`) — without needing to change a single line of existing code. Immediate benefit: testing/debugging is now as simple as `qemu ... -serial file:log.txt` followed by `cat log.txt`, eliminating the need for screendumps or manually reading physical memory VGA buffers like in previous updates.

### File Structure (Update)
- `serial.h/.c` — COM1 driver (`serial_init`, `serial_putchar`, `serial_write`)
- *(Previous files remain the same as the last update)*

**Major update:** Proper heap allocator (`kmalloc`/`kfree`, free-list, first-fit + split + merge-forward), automatically extending via PMM+VMM (new `vmm_map_page()`, capable of creating page tables on-demand). Verified running smoothly: verified directly by reading the VGA text buffer contents via QEMU monitor (not screenshots), the `alloctest` command confirmed that `kfree()` + free block reuse work correctly (the address of the newly allocated block matches the exact address of the newly freed block).

**Known limitations documented in code** (not hidden):
- `kfree()` only merges forward, not backward yet — requires a doubly-linked list for that.
- `vmm_map_page()` explicitly PANICS if a new page table is needed at a physical address >4MB (outside the identity-map) — temporary mapping for this case is not implemented yet, though it rarely happens at this stage.

### File Structure (Update)
- `heap.h/.c` — kmalloc/kfree, free-list allocator
- `vmm.c` — added public `vmm_map_page()` (previously only identity-mapped once during boot)
- *(Previous files remain the same as the last update)*

What it can do so far:
- Boots via a bootloader (GRUB, Multiboot)
- GDT installed (flat memory model, with ring3 slots ready for future usermode)
- IDT + PIC remapped, CPU exceptions (0-31) and IRQs (32-47) handled properly
- IRQ-driven keyboard — CPU executes `hlt` when idle, no more 100% CPU busy-loops
- Text printing to screen + auto-scrolling
- Interactive shell: `help`, `clear`, `mem`

### Building & Testing (For the brave)

```bash
cd OS
make
make run   # requires qemu-system-i386
```

*(You'll need nasm, an i686-elf-gcc cross-compiler, qemu, etc. Setup is a bit tedious—not really meant for beginners)*

### Goals / Roadmap
Next up, in prioritized order:
1. ~~Paging / virtual memory~~ ✅ Done (v0.2.0)
2. ~~Proper Heap (`kmalloc`/`kfree`)~~ ✅ Done (v0.3.0)
3. Ring 0 → Ring 3 (usermode + syscalls)
4. Disk driver (ATA/AHCI) + simple filesystem
5. ELF loader — so it can run actual external executables
6. Multitasking (requires a proper heap + per-task stacks first)

If you're bored out of your mind and love messing around with low-level dev, feel free to fork & join the club 😂

Made with ❤️ + coffee + insomnia  
by *Makluk Gabut*
