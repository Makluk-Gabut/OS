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
**v0.3.1** — masih berlanjut (kalo ada waktu luang)

**Update:** Serial port driver (COM1, polling, 38400 8N1). Semua output `print_string`/`print_dec`/`print_hex` sekarang otomatis ke-mirror ke serial juga (lewat `print_char` di `screen.c`) — gak perlu ubah satu pun kode existing. Manfaat langsung: testing/debug sekarang tinggal `qemu ... -serial file:log.txt` lalu `cat log.txt`, gak perlu lagi screendump atau baca physical memory VGA buffer manual kayak update-update sebelumnya.

### Struktur file (update)
- `serial.h/.c` — driver COM1 (`serial_init`, `serial_putchar`, `serial_write`)
- (file-file sebelumnya tetap sama seperti update terakhir)

**Major update:** Heap allocator proper (`kmalloc`/`kfree`, free-list, first-fit + split + merge-forward), otomatis extend lewat PMM+VMM (`vmm_map_page()` baru, bisa bikin page table on-demand). Diverifikasi jalan beneran: baca langsung isi VGA text buffer via QEMU monitor (bukan screenshot), command `alloctest` kekonfirmasi `kfree()` + reuse blok bebas bekerja benar (alamat blok baru persis sama dengan alamat blok yang baru di-`free()`).

**Keterbatasan yang disadari & didokumentasikan di kode** (bukan disembunyikan):
- `kfree()` cuma merge maju (forward), belum merge mundur — butuh doubly-linked list buat itu
- `vmm_map_page()` PANIC eksplisit kalau butuh page table baru di physical address >4MB (di luar identity-map) — belum ada temporary mapping buat kasus itu, tapi jarang kejadian di tahap sekarang

### Struktur file (update)
- `heap.h/.c` — kmalloc/kfree, free-list allocator
- `vmm.c` — tambah `vmm_map_page()` publik (sebelumnya cuma identity-map sekali pas boot)
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
2. ~~Heap proper (`kmalloc`/`kfree`)~~ ✅ selesai (v0.3.0)
3. ~~Serial driver~~ ✅ selesai (v0.3.1)
4. ~~Ring 0 → Ring 3 (usermode + syscall)~~ ✅ selesai (v0.4.0)
5. ~~Driver disk (ATA PIO) + filesystem flat sederhana~~ ✅ selesai (v0.5.0)
6. ~~ELF loader~~ ✅ selesai (v0.6.0)
7. ~~Multitasking (round-robin, ring0+ring3 campur, sleep)~~ ✅ selesai (v0.7.0)
8. ~~Cara "bunuh" task yang crash tanpa halt seluruh kernel~~ ✅ selesai (v0.8.0)
9. ~~Preemption adil~~ — ternyata udah otomatis ada dari desain scheduler_tick (switch tiap PIT tick ~10ms), diklarifikasi di v0.8.0. Satu-satunya celah nyata: task ring0 yang cli+infinite-loop (inherent trust boundary, bukan bug scheduler).
10. ~~Isolasi proses beneran — page directory terpisah per task~~ ✅ selesai (v0.8.0)
11. ~~Reclaim physical stack page task yang di-kill~~ ✅ selesai (v0.8.0)
12. ~~Reclaim code/data page + page directory milik task isolated yang di-kill~~ ✅ selesai (v0.9.0)
13. ~~Isolasi otomatis buat task hasil command `run` biasa~~ ✅ selesai (v0.9.0)
14. ~~Filesystem delete + reclaim ruang disk beneran~~ ✅ selesai (v1.0.0)
15. ~~Memory quota per-task~~ ✅ selesai (v1.0.0)

**GabutOS v1.0.0 — rilis stabil pertama.** Semua item roadmap awal tuntas, termasuk tiga gap yang sengaja ditahan dari v0.9.0. Sisa keterbatasan didokumentasikan jujur di README per bagian fitur, bukan lagi dianggap "belum selesai".

### Roadmap Selanjutnya (post-1.0.0)
Target besar berikutnya: **self-hosted C compiler + shell interaktif yang lebih dari sekadar command tetap**, biar orang beneran bisa nulis dan compile program *di dalam* GabutOS sendiri — bukan cuma jalanin ELF yang dikompilasi di luar. Ini lompatan besar (compiler C itu proyek sendiri yang berat), kemungkinan dipecah jadi:
1. Shell yang lebih powerful — script sederhana, variabel, pipe antar command
2. Text editor minimal (buat nulis kode di dalam GabutOS)
3. Compiler C subset kecil dulu (bukan C penuh — mungkin cuma cukup buat compile ulang beberapa demo program sendiri)
4. Assembler + linker minimal biar compiler bisa hasilkan ELF yang langsung jalan lewat loader yang udah ada

Kalau lu juga lagi gabut dan suka ngoprek low-level, silakan fork & ikut gabut bareng 😂

Made with ❤️ + kopi + insomnia
by Makluk Gabut
