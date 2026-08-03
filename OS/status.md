# GabutOS

Proyek **operating system** dari nol yang dibuat pas lagi gabut berat.

Ini bukan Windows, bukan Linux, bukan juga macOS.
Ini cuma eksperimen low-level gue buat belajar gimana komputer bener-bener jalan dari boot sampe nunjukin tulisan di layar.

### Struktur file
- `assembler.asm` → entry point, multiboot header, stack setup
- `kernel.c` → orkestrator utama + shell
- `screen.c/.h` → driver VGA text mode
- `gdt.c/.h` + `gdt_flush.asm` → Global Descriptor Table
- `idt.c/.h` + `idt_flush.asm` + `isr.asm` + `isr.c` → Interrupt Descriptor Table, remap PIC, handler exception & IRQ
- `keyboard.c/.h` → driver keyboard IRQ-driven (bukan polling lagi)
- `io.h` → helper `inb`/`outb`
- `linker.ld` → linker script

### Status Saat Ini
**v0.2.0** — masih berlanjut (kalo ada waktu luang)

**Major update:** Physical Memory Manager (bitmap allocator, parsing memory map dari GRUB) + Paging (identity-map 4MB pertama, CR0.PG aktif, page fault handler). Diverifikasi jalan beneran di QEMU: CR0/CR3 diperiksa, PIT tetap nembak normal setelah paging aktif, zero crash.

### Struktur file (update)
- `multiboot.h` — struct info dari GRUB (memory map, dll)
- `pmm.c/.h` — Physical Memory Manager, bitmap allocator, cap 256MB
- `vmm.c/.h` — Virtual Memory Manager, setup page directory/table, page fault handler
- (file-file sebelumnya tetap sama seperti update terakhir)

Sekarang baru bisa:
- Boot via bootloader (GRUB, Multiboot)
- GDT terpasang (flat memory model, sudah ada slot ring3 buat usermode nanti)
- IDT + PIC remap, exception CPU (0-31) dan IRQ (32-47) ke-handle dengan benar
- Keyboard IRQ-driven — CPU `hlt` waktu idle, nggak busy-loop 100% lagi
- Print teks ke layar + scroll
- Shell interaktif: `help`, `clear`, `mem`

### Cara Ngebuild & Test (buat yang berani)

```bash
cd OS
make
make run   # butuh qemu-system-i386
```

(Butuh nasm, i686-elf-gcc cross-compiler, qemu, dll. Setupnya agak ribet, ini bukan buat pemula)

### Tujuanku
Roadmap berikutnya, urut prioritas:
1. ~~Paging / virtual memory~~ ✅ selesai (v0.2.0)
2. Heap proper (tambah `free()`, ganti bump allocator ke free-list, sekarang udah punya PMM buat basis-nya)
3. Ring 0 → Ring 3 (usermode + syscall)
4. Driver disk (ATA/AHCI) + filesystem sederhana
5. ELF loader — biar bisa jalanin program eksternal beneran
6. Multitasking (butuh heap + stack per-task yang udah proper dulu)

Kalau lu juga lagi gabut dan suka ngoprek low-level, silakan fork & ikut gabut bareng 😂

Made with ❤️ + kopi + insomnia
by Makluk Gabut
