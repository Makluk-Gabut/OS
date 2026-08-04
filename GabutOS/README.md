# GabutOS

A hobby x86 kernel built from scratch, booted via GRUB (Multiboot1). This document explains the design rationale behind every module — the "why", not just the "what" — since the source files themselves are kept comment-free.

## Boot Sequence

`GRUB` loads the kernel per the Multiboot1 spec. The Multiboot header (magic `0x1BADB002`, alignment + memory-info flags, checksum) lives in `assembler.asm` and must appear within the first 8KB of the file for GRUB to recognize it.

At entry (`_start`):
1. Stack pointer is initialized (16 bytes aligned, per the x86 C ABI requirement).
2. Interrupts stay off (`cli`) — this is safe because `kernel_main()` installs GDT, IDT, and remaps the PIC before ever calling `sti` itself.
3. GRUB leaves `EAX` = Multiboot magic (`0x2BADB002`) and `EBX` = pointer to the `multiboot_info` struct. Following cdecl (arguments pushed right-to-left), `EBX` is pushed first, then `EAX`, so `kernel_main(magic, mb_info_addr)` receives them correctly.
4. A hang loop (`cli; hlt; jmp`) exists as a safety net, though it should never be reached since `shell()` runs forever.

`linker.ld` places the kernel starting at 1MB (the conventional load address GRUB uses) and exports `kernel_start`/`kernel_end` symbols so the physical memory manager knows which physical pages the kernel itself occupies.

## GDT (`gdt.c`, `gdt_flush.asm`)

Five flat-model descriptors: null, kernel code (ring 0), kernel data (ring 0), user code (ring 3), user data (ring 3). The user-mode descriptors are set up now even though nothing uses them yet — they're there for when usermode is implemented later.

`gdt_flush.asm` loads the GDT via `lgdt`, then reloads every segment register: `DS/ES/FS/GS/SS` get the kernel data selector (`0x10` = index 2 × 8), and a far jump to `0x08:.flush` (kernel code selector, index 1 × 8) reloads `CS`, which can't be set with a plain `mov`.

## IDT / Interrupts (`idt.c`, `isr.asm`, `interrupts.c`, `io.h`)

256 IDT gates are populated: 32 CPU exceptions (vectors 0–31) and 16 hardware IRQs (vectors 32–47). The PIC is remapped first — by default, IRQ0–15 fire on interrupts 8–15 and 0x70–0x77, which collide with CPU exception vectors. Remapping moves them to 32–47, out of the way.

`isr.asm` defines two macros: `ISR_NOERRCODE` for exceptions where the CPU doesn't push an error code (a dummy `0` is pushed instead, so the stack layout stays consistent across all exception handlers), and `ISR_ERRCODE` for the ones that do (double fault, GPF, page fault, etc.). Both funnel into a common stub that saves all registers (`pusha`), switches to the kernel data segment, calls the C-level dispatcher, restores everything, and `iret`s.

`interrupts.c` (originally named `isr.c`) holds `isr_handler()` (panics with the exception name if unregistered) and `irq_handler()` (sends End-Of-Interrupt to the PIC — and to the slave PIC too, if the IRQ came from vector ≥40 — before dispatching to the registered handler).

**Naming note:** the C file is called `interrupts.c`, not `isr.c`, because `isr.c` and `isr.asm` would both compile to `isr.o` and silently overwrite each other during the build — a real bug that was caught by actually compiling and testing, not just by reading the code.

## PIT Timer (`pit.c`)

Configured for 100Hz (channel 0, mode 3 square wave, lobyte/hibyte access). `1193182 / hz` gives the 16-bit divisor written to the PIT. `sleep_ms()` converts milliseconds to ticks and `hlt`s the CPU until the tick counter reaches the target — this saves power compared to a busy-wait spin loop, since `hlt` lets the CPU idle until the next interrupt wakes it.

## Keyboard (`keyboard.c`)

IRQ1-driven, not polled. A small circular buffer holds incoming characters; the interrupt handler reads the scancode from port `0x60`, discards key-release events (bit 7 set), maps the scancode to ASCII via a lookup table, and pushes it into the buffer. `keyboard_getchar()` blocks by `hlt`-ing until the buffer is non-empty — power-efficient, and it composes naturally with the PIT interrupt also firing during the wait.

## VGA Screen + Serial (`screen.c`, `serial.c`)

`screen.c` writes directly to the VGA text buffer at `0xB8000`, handles newline/backspace, and auto-scrolls when the cursor passes the last row.

`serial.c` drives COM1 (`0x3F8`) at 38400 baud, 8N1, with FIFO enabled. It's polling-based (no interrupts) since kernel logging doesn't need to be non-blocking. `print_char()` in `screen.c` mirrors every character to serial as well, so all existing `print_string`/`print_dec`/`print_hex` calls anywhere in the kernel automatically get logged to serial without needing to touch those call sites. This makes debugging dramatically easier: `qemu ... -serial file:log.txt` produces a plain-text boot log instead of needing a VGA screenshot or a manual VGA-buffer memory dump to verify output.

## Physical Memory Manager (`pmm.c`, `multiboot.h`)

A bitmap allocator capped at 256MB (`MAX_SUPPORTED_MEM`) — comfortably more than QEMU's default 128MB and plenty for a hobby OS at this stage; raising the cap later just means growing the bitmap.

Initialization defaults every page to "used" (the safe default), then walks the Multiboot memory map (`mbi->mmap_addr`/`mmap_length`) and clears the bit for each page inside an `available`-type region. Per the Multiboot spec, each `mmap` entry's `size` field does *not* include itself, so the iteration step is `entry->size + sizeof(entry->size)`.

If GRUB doesn't provide a detailed memory map, it falls back to `mem_upper` (kilobytes of memory starting at 1MB) as a coarser estimate. If neither flag is set, every page stays marked "used" — `pmm_alloc_page()` will always fail rather than silently handing out memory that was never confirmed available.

Two regions are force-reserved regardless of what the memory map claims:
- The first 1MB (256 pages) — the IVT, BIOS data area, and the VGA text buffer all live here.
- The kernel's own physical footprint, computed from the linker-exported `kernel_start`/`kernel_end` symbols — otherwise the allocator could hand out memory the kernel is actively running from.

## Paging / Virtual Memory Manager (`vmm.c`)

`vmm_init()` identity-maps the first 4MB (one page directory entry pointing at one page table, 1024 × 4KB entries) — enough to cover the kernel and the VGA buffer, which conveniently falls within this range. `CR3` is loaded with the page directory's physical address, then `CR0`'s PG bit (bit 31) is set to actually turn paging on. A page fault handler is registered on vector 14 that reads `CR2` (the faulting address) and decodes the error code's bits (present/write/user) before halting.

`vmm_map_page()` extends mappings beyond that initial 4MB, allocating new page tables on demand via the PMM. **This has a known, deliberate limitation:** a freshly allocated page table has to be zeroed out, and that's only safe through an address that's already mapped. Since the identity-map only covers the first 4MB, if `pmm_alloc_page()` ever returns a physical address at or above `0x400000` for a *new page table itself*, there's currently no way to safely zero it — a temporary mapping mechanism for that case hasn't been implemented yet. Rather than risk silent memory corruption, `vmm_map_page()` panics explicitly if this happens. In practice it's unlikely to trigger at this stage, since the PMM hands out low-numbered (and thus low-address) pages early on — but it's a real edge case worth fixing before the heap or any allocator sees much heavier use.

## Heap Allocator (`heap.c`)

A free-list allocator (`kmalloc`/`kfree`) that replaced an earlier bump allocator (which had no `free()` at all). The heap lives in its own virtual address range, `0x400000`–`0x800000` (right after the initial identity-mapped region, up to a 4MB cap for now), and grows lazily: it only calls `vmm_map_page()` for a new physical page when it actually needs more space.

`kmalloc()` is first-fit: it walks the block list looking for a free block big enough, splitting it if the leftover is large enough to be useful on its own. If nothing fits, it extends the heap sbrk-style — mapping new pages as needed and appending a fresh block at the end.

`kfree()` marks a block free and merges it forward with the next block if that one is also free and directly adjacent in memory. **Known limitation:** it only merges forward, not backward — a proper backward merge would need a doubly-linked list, which hasn't been added yet. This means some fragmentation can accumulate in patterns a smarter allocator would avoid, though it doesn't affect correctness.

## Shell (`kernel.c`)

A simple line-based REPL. Commands: `help`, `clear`, `mem` (heap stats), `uptime` (PIT ticks), `pages` (physical memory stats), `alloctest` (allocates three blocks, frees the middle one, allocates a new block, and reports whether it landed in the freed block's old address — a live demonstration that `kfree()` reuse is actually working, not just "not crashing").

## Verified Behavior

Every major feature in this repo was checked by actually compiling and booting the kernel in QEMU — not just read for correctness. That included: confirming `CR0`/`CR3` register state after enabling paging, confirming the PIT interrupt kept firing normally after paging was turned on (proving the transition didn't break interrupt handling), reading the VGA text buffer directly out of guest physical memory to confirm shell output byte-for-byte, and — since the serial driver was added — simply reading the QEMU serial log file, which is now the easiest way to check behavior end-to-end.

Three real bugs were found this way (not from code review alone):
1. `isr.c` and `isr.asm` both compiling to `isr.o`, silently overwriting each other and leaving `isr_handler`/`irq_handler` unlinked — fixed by renaming the C file to `interrupts.c`.
2. The linker inserting a `.note.gnu.build-id` section before `.text`, pushing the Multiboot header past the 8KB boundary GRUB scans — fixed with `--build-id=none`.
3. Modern GCC defaulting to PIE executables, which GRUB can't load as a flat kernel — fixed with `-no-pie`.

## Roadmap

1. ~~Paging / virtual memory~~ — done (v0.2.0)
2. ~~Heap (`kmalloc`/`kfree`)~~ — done (v0.3.0)
3. ~~Serial driver~~ — done (v0.3.1)
4. Ring 0 → Ring 3 (usermode + syscalls) — next
5. Disk driver (ATA/AHCI) + a simple filesystem
6. ELF loader
7. Multitasking (PIT-driven context switching)

## Building

```bash
cd OS
make
make run   # requires qemu-system-i386
```

Requires `nasm`, an `i686-elf-gcc` cross-compiler, and `qemu-system-i386`. A secondary `Makefile.tested-with-gcc-m32` is included as a fallback for environments without the cross-compiler (uses host `gcc -m32` plus a few extra linker flags to compensate for differences from a true `i686-elf` toolchain).
