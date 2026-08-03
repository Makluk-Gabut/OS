# GabutOS

An **operating system** built from scratch out of pure boredom.

This isn't Windows, it isn't Linux, and it definitely isn't macOS. 
It's just my low-level experiment to learn how a computer actually boots up, executes code, and prints stuff on the screen.

---

## 📂 File Structure

* `assembler.asm` → Entry point, multiboot header, stack setup
* `kernel.c` → Main orchestrator + shell
* `screen.c/.h` → VGA text mode driver
* `gdt.c/.h` + `gdt_flush.asm` → Global Descriptor Table
* `idt.c/.h` + `idt_flush.asm` + `isr.asm` + `interrupts.c` → Interrupt Descriptor Table, PIC remapping, exception & IRQ handlers
* `keyboard.c/.h` → IRQ-driven keyboard driver (no more polling!)
* `pit.c/.h` → Programmable Interval Timer (100Hz) & uptime tracker
* `pmm.c/.h` → Physical Memory Manager (bitmap allocator)
* `vmm.c/.h` → Virtual Memory Manager (Paging)
* `io.h` → Low-level `inb`/`outb` helpers
* `linker.ld` → Linker script

---

## ⚡ Current Status & Version History

- [x] **GDT** (flat memory model + Ring 3 slots) — `v0.1.x`
- [x] **IDT + PIC remap + exception/IRQ handling** — `v0.1.x`
- [x] **Keyboard IRQ-driven** — `v0.1.x`
- [x] **PIT timer (100Hz) + uptime** — `v0.1.3`
- [x] **Paging** (PMM bitmap + identity-map first 4MB + page fault handler) — `v0.2.0`
- [x] **Proper Heap** (`kmalloc`/`kfree`, free-list allocator) — `v0.3.0`
- [ ] **Ring 0 → Ring 3** (usermode + syscall interface) — *Next up*
- [ ] **Disk driver** (ATA/AHCI) + simple filesystem
- [ ] **ELF loader** (run external programs)
- [ ] **Multitasking** (PIT-based context switching)

---

## ⚠️ Documented Technical Debt

*(So I don't forget later)*

- `kfree()` only merges forward, not backward yet (requires a doubly-linked list).
- `vmm_map_page()` panics if it needs a new page table outside the first 4MB identity-mapped area (temporary mapping not implemented yet).
- `MAX_SUPPORTED_MEM` in PMM is still capped at 256MB (fine for now, but hardcoded).

---

## 🛠️ Building & Testing (For the brave)

```bash
cd OS
make
make run   # requires qemu-system-i386
```

> **Note:** You'll need `nasm`, an `i686-elf-gcc` cross-compiler, `qemu`, etc. Setup is a bit tedious—not really meant for beginners.

---

## 🙏 A Little Apology

Sorry about jumping straight to **v0.3.0**! This project is being developed alongside **TIniMind**, so managing the time between the two got a bit messy and chaotic. 

check out the 0.0.1 version on my other repo: https://github.com/Makluk-Gabut/Gabut-Playground

If you're bored out of your mind and love messing around with low-level dev, feel free to fork it and join the club 😂

Made with ❤️ + coffee + insomnia  
by *Makluk Gabut*
