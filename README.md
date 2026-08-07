# GabutOS

A hobby x86 kernel built from scratch, booted via GRUB (Multiboot1). This document explains the design rationale behind every module — the "why", not just the "what" — since the source files themselves are kept comment-free.

## Directory Structure

```
GabutOS/
├── kernel.c          orchestrator: boots subsystems, shell, command dispatch
├── boot/             Multiboot header, entry point, linker script
├── cpu/              GDT, IDT, exception/IRQ handling, TSS, ring0<->ring3, port I/O
├── mm/                physical + virtual memory management, heap allocator
├── drivers/          VGA, serial, keyboard, PIT timer, ATA disk
├── fs/                flat filesystem
├── loader/            ELF32 parser and loader
└── task/              task control blocks and the scheduler
```

Files reference each other with plain `#include "header.h"` regardless of which folder they're actually in — the Makefile's `-Icpu -Imm -Idrivers -Ifs -Iloader -Itask` flags handle path resolution, so no include ever needs a relative path like `../cpu/idt.h`.

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

## Usermode (Ring 0 → Ring 3) (`tss.c`, `usermode.c`, `usermode_asm.asm`)

A minimal TSS (Task State Segment) is installed in GDT slot 5. In this design the TSS's only real job is holding `ss0`/`esp0` — the kernel stack the CPU should switch to automatically whenever an interrupt or syscall arrives while running in ring 3. (The other TSS fields exist only because the struct format requires them; they're not used for hardware task-switching, which this kernel doesn't do.)

`enter_usermode()` (assembly) performs the ring0→ring3 transition: it reloads the data segment registers with the ring-3 selector, then manually pushes the five values `iret` expects — `SS`, `ESP`, `EFLAGS`, `CS`, `EIP` — and executes `iret`, which the CPU interprets as "return" to a lower privilege level, changing CPL to 3 in the process. The function takes care to load the entry point and user stack address into `ECX`/`EDX` *before* touching `AX` for the segment reload, since overwriting `AX` would otherwise clobber whichever argument happened to share its register.

A syscall gate is installed at interrupt vector `0x80` (128) with DPL=3 in its IDT flags (`0xEE` instead of the usual `0x8E`) — this is the one bit that actually lets ring-3 code invoke `int 0x80` at all; every other gate stays DPL=0 and would fault if called from ring 3. The handler (`usermode.c`) currently implements a single syscall, `SYS_PRINT`, which writes the character passed in `EBX` to the screen — proving that ring-3 code can only reach the display through a kernel-mediated call, not by touching the VGA buffer through its own instructions.

The demo (`usermode` shell command) allocates one physical page for a user stack, maps it at `0xA00000` with the `PAGE_USER` bit set, jumps into a small `ring3_task()` function that prints a message via syscalls, and then **deliberately executes `cli`** — a privileged instruction. Since `cli` is checked purely against CPL regardless of paging permissions, this reliably triggers a General Protection Fault (vector 13), which the existing exception handler catches and reports before halting. This is the actual proof that ring-3 isolation is enforced by the CPU, not just that the `iret` "worked" without crashing.

**Known, deliberate limitations at this stage — this is a proof-of-concept transition, not process isolation:**
- `ring3_task()`'s compiled code physically lives inside the kernel's own identity-mapped first 4MB. To let the CPU even *fetch* its instructions from ring 3, the `PAGE_USER` bit had to be added to that entire identity-mapped region (previously kernel-only). That means, as of this version, ring-3 code could in principle also reach the VGA buffer or other low-memory structures directly, bypassing the syscall path — the syscall demo proves the *mechanism* works, but doesn't yet enforce that it's the *only* path.
- The `usermode` command is one-shot by design: once `cli` triggers the GPF, the kernel halts permanently (the default panic behavior for any unregistered/fatal exception). There's no scheduler yet to tear down a faulting task and return control to the shell — that requires the multitasking milestone still ahead on the roadmap.
- Real process isolation (a user program that *can't* read kernel memory or other processes' memory at all) needs a separate page directory per process, not just a `PAGE_USER` bit sprinkled onto a single shared address space. That's a substantially larger undertaking than what's here.

## Disk Driver + Filesystem (`ata.c`, `fs.c`)

`ata.c` drives the primary ATA channel in PIO mode (ports `0x1F0`–`0x1F7`), LBA28 addressing, one sector (512 bytes) per operation — no DMA, no IRQ-driven transfer, purely polling `BSY`/`DRQ` status bits. This is the simplest correct way to talk to a disk and matches the polling pattern already used elsewhere in the kernel before the PIT/keyboard moved to interrupts.

`fs.c` implements a deliberately minimal flat filesystem — no directories, no permissions, no deletion, nothing resembling FAT or ext2:

- **Sector 0**: superblock (magic number, file count, and `next_free_lba` — a running pointer for the next unused data sector).
- **Sectors 1–2**: a fixed file table, 32 entries × 32 bytes each (name, start sector, size in bytes, in-use flag) — sized so that exactly 16 entries fit per 512-byte sector, avoiding wasted space.
- **Sector 3 onward**: file data, allocated by simply bumping `next_free_lba` forward by however many sectors a new file needs. There's no free-space reclamation — deleting a file (which isn't implemented anyway) wouldn't actually free its sectors for reuse.

`fs_write()` finds an empty file-table slot, writes the data sector-by-sector (zero-padding the last partial sector), then updates and re-writes both the file table and the superblock. `fs_read()` looks up a file by exact name match and copies its sectors into the caller's buffer. `fs_mount()` reads the superblock and checks the magic number — if it doesn't match (a blank or foreign disk), mounting fails cleanly rather than treating garbage as a valid filesystem.

### Verified Persistence

This was tested across two *separate* QEMU boots against the same disk image file — not just within a single running session:

1. **First boot**: formatted the disk, wrote a file, read it back, confirmed via serial log.
2. **QEMU fully exited.**
3. **Second boot, same disk image, no format or write commands issued**: `fs_mount()` succeeded (`[ok] Filesystem di-mount dari disk`), and `ls`/`cat` immediately showed the file written in the *previous* session — proving the data survived on the simulated disk itself, not just in RAM.

### Known Limitations

- No deletion, no free-space reclamation — the data area only ever grows.
- No subdirectories; all files live in one flat table capped at 32 entries.
- No collision/overwrite protection — writing a file with a name that already exists creates a second entry rather than replacing the first.
- Single fixed drive (primary master) assumed; no drive detection or multi-disk support.

## ELF Loader (`elf.c`)

`elf.c` parses a 32-bit ELF executable already sitting in a memory buffer (typically loaded there via `fs_read()`) and maps it into the address space so it can be jumped into via the existing `usermode_jump()` (the stack-setup-and-`enter_usermode()` helper factored out of the earlier usermode demo).

Validation happens in order: file large enough to even contain an ELF header, magic bytes match `\x7fELF`, `e_type == ET_EXEC` (statically linked executable — no dynamic linking support at all), `e_machine == EM_386`, and the program header table falls within the file's actual bounds. For every `PT_LOAD` segment, its `p_vaddr` must be at or above `0x1000000` (16MB) — this is a hard floor, rejecting any segment that would land inside the kernel's own identity-mapped region (0–4MB), the heap's virtual range (4–8MB), or the fixed usermode demo stack (10MB) from the earlier usermode work. There's nothing sophisticated behind that number; it's just comfortably clear of everything else the kernel already uses.

For each valid `PT_LOAD` segment, the loader page-aligns the segment's start and end, then for every page in that range not already mapped (checked via a new `vmm_is_mapped()` helper — this matters because two segments can share a boundary page, and mapping it twice would silently leak the first segment's already-written data along with a physical page) it allocates a fresh physical page and maps it with `PAGE_USER` set, so the code becomes both executable and readable from ring 3. The segment's file bytes are then copied in, and anything beyond `p_filesz` up to `p_memsz` is zeroed — that's the `.bss` region, which doesn't take up space in the file at all.

### Verified End-to-End

This wasn't tested with a synthetic or hand-crafted ELF — a real, independently compiled program was used:

1. A minimal freestanding C program (`_start()`, no libc, invokes `int 0x80` with `eax=1` to print each character) was compiled and linked with `ld -T linker.ld -n` against a linker script placing `.text` at `0x1000000`, producing a genuine statically-linked `ET_EXEC`/`EM_386` binary confirmed via `readelf`.
2. That binary's raw bytes were written to the GabutOS disk via `fs_write()` (through a temporary `loadtest` shell command — see the limitation note below on why this exists).
3. `ls` confirmed the file landed correctly with the right size.
4. `run hello.elf` triggered the full pipeline — read from disk, parse the ELF header, map its one `PT_LOAD` segment, jump to its entry point in ring 3 — and the program's own `int 0x80` syscall calls printed its message, confirming genuinely correct header parsing, segment mapping, and entry-point computation, not just "didn't crash."

```
GabutOS> run hello.elf
[elf] load sukses, lompat ke entry point...
Halo dari program ELF asli yang di-load dari disk!
```

Unlike the earlier `usermode` demo (which deliberately faults on a privileged `cli` to prove CPL enforcement), this program spins peacefully in an infinite loop afterward — a "well-behaved" complement to that earlier "misbehaving" demo, and further evidence the kernel didn't crash getting there.

### Known Limitations

- **No real way yet to get an arbitrary compiled program onto the disk from the host.** The `loadtest` command exists purely as a bootstrapping trick — it writes a byte array (`loader/test_program.h`) that was generated once by hand from a compiled ELF and embedded directly into the kernel source. This obviously doesn't scale to testing arbitrary programs; a proper host-side tool that writes files directly onto the raw disk image (mimicking `fs.c`'s on-disk layout without going through the kernel at all) is a clear near-term follow-up, separate from the loader itself.
- No dynamic linking — only flat, statically-linked `ET_EXEC` binaries.
- No meaningful security validation beyond the `0x1000000` floor — a maliciously crafted ELF could still target addresses that, while above the floor, overlap something else the kernel is using, or exhaust physical memory through an enormous `p_memsz`. Real protection needs the same per-process address space isolation noted as missing in the usermode section.
- Like the `usermode` demo, if the loaded program does something that faults (bad memory access, privileged instruction, etc.), the kernel halts — there's still no way to tear down a single failed process and return to the shell.

## Multitasking (`task/task.c`, `task/scheduler.c`)

This is the largest structural change in the kernel so far, and the one piece explicitly built to run ring 0 and ring 3 tasks side by side in the same scheduler — a deliberately harder path than restricting everything to one ring, chosen because the tasks it schedules come straight from the ELF loader, and a real workload mixes both.

### The Core Trick: Reusing the Interrupt Frame

Rather than building a separate context-switch code path, the scheduler piggybacks on the interrupt mechanism that already exists. When the PIT fires, `irq_common_stub` (in `isr.asm`) already pushes a complete snapshot of the interrupted task's state onto its own kernel stack — general registers via `pusha`, then `EIP`/`CS`/`EFLAGS` (and `ESP`/`SS` too, if the interrupt came from ring 3) pushed automatically by the CPU itself. That pushed frame is a task's entire suspended state, sitting right there on its stack.

So switching tasks means: save the *current* stack pointer (it now points at a complete, valid frame), pick a *different* task's previously-saved stack pointer, and load that instead — before the stub's `popa` and `iret` run. `iret` doesn't know or care whether it's resuming ring 0 or ring 3 code; it reads `CS` off the frame and switches privilege level automatically. One assembly path, both kinds of task, no branching on ring type anywhere in the switch logic.

To make this possible, `irq_common_stub` was changed to pass `esp` as an actual argument to `irq_handler` (previously it passed the interrupt frame by value, which is fine for reading but useless for a scheduler that needs to substitute a different stack), and to load whatever `irq_handler` returns back into `esp` before `popa`/`iret` run. `irq_handler` itself now returns `scheduler_tick()`'s result — the *next* task's saved stack pointer if a switch happened, or the same one unchanged if not. `isr_common_stub` (used by CPU exceptions and the `int 0x80` syscall gate) got the identical treatment, for a reason covered below.

### Building a Task From Nothing (`task_create`)

A brand-new task obviously has no "previously interrupted" stack frame to resume — one has to be manufactured by hand, matching the exact byte layout the interrupt stub expects, so that the very first time this task is switched to, the stub's `popa`/`iret` sequence reconstructs a plausible "return" into the task's actual entry point instead of garbage.

Two frame layouts exist, matching whether the CPU pushed `ESP`/`SS` (ring 3 target) or not (ring 0 target) — this mirrors exactly how the CPU behaves on a real interrupt: it only pushes the extra stack-switch fields when privilege level changes. Ring-3 tasks additionally get a dedicated one-page user stack allocated and mapped with `PAGE_USER`, separate from the kernel stack the interrupt frame itself lives on (a ring-3 task always has *two* stacks: the kernel one used only while inside an interrupt/syscall, and its own user-mode one used for everything else — this is exactly what the TSS's `esp0` field exists to point the CPU toward automatically).

### Scheduler: Flat Round-Robin

Initially the scheduler used four separate priority queues, always draining the highest non-empty one first. **This starved lower-priority tasks completely** in testing — the interactive shell (naturally given the highest priority) never blocks waiting for a syscall the way the demo tasks do; it just `hlt`s waiting for a keystroke, which still counts as "ready" from the scheduler's point of view. It kept winning every tick, and `A`/`B`/`C` from the lower-priority demo tasks never appeared at all in the first test run. The fix was to drop strict priority levels for a single flat FIFO ready queue — every ready task gets an equal turn, priority is still stored per-task for later use (e.g. weighting how *long* a turn lasts) but no longer decides *whether* a task's turn ever comes at all.

### A Second Real Bug: Syscalls Needed To Switch Too

`task_sleep_ms()` is invoked from the `SYS_SLEEP` syscall handler, reached through `int 0x80` — which goes through `isr_common_stub`, not `irq_common_stub`. Before that stub got the same "pass esp, load back the return value" treatment as the IRQ path, a sleeping task's syscall would mark it `TASK_SLEEPING` and then just... keep running anyway, because nothing actually swapped its stack out. Confirmed by an actual serial-log capture during testing: the log showed a single character being printed continuously without ever alternating, a strong hint that a task was executing well past the point it should have yielded. The fix mirrors the PIT path closely, but only forces a switch when a task genuinely just asked to sleep (`scheduler_maybe_switch()`, gated by a `pending_switch` flag set inside `task_sleep_ms()`) rather than on every syscall unconditionally.

### Verified Behavior

Three ELF tasks (two ring-3, one ring-0 — genuinely different privilege levels in the same run) were started via the `multitask` shell command, each printing a distinct letter (`A`, `B`, `C`) in a `print`-then-`sleep` loop with different sleep intervals, while the shell itself (also just another task in the same scheduler) remained fully interactive. Captured live from the serial log:

```
GabutOS> multitask
3 task ditambahkan (A=ring3 prio1, B=ring3 prio2, C=ring0 prio1).
GabutOS> ABCABACABACABABACABACABpAs
  * shell (running, ring0, prio 0)
    task-A(r3) (sleeping sampai tick 888)
    task-B(r3) (sleeping sampai tick 878)
    task-C(r0) (sleeping sampai tick 878)
GabutOS> BCAABACABuptAimCe
Ticks: 1026 (~10 detik sejak boot)
```

The `p` and `s` from typing `ps`, and `uptAimCe` from typing `uptime`, are visibly interleaved character-by-character with the `A`/`B`/`C` output — direct proof the shell (a keyboard-driven, ring-0 task) and the two ring-3 letter-printing tasks were genuinely being time-sliced against each other, not run sequentially. `ps` correctly reported all three background tasks as `sleeping` with distinct wake ticks matching their different sleep durations. The system ran continuously for over 15 seconds with output still alternating correctly at the end, with zero crashes beyond the two ordinary QEMU startup resets that appear in every test in this document.

### Known Limitations

- **No way yet to kill a faulting task.** This closes part of the gap noted in the usermode and ELF-loader sections, but only partly: a task that sleeps and wakes up cooperates fine, but a task that page-faults or executes a privileged instruction still triggers the kernel-wide panic-and-halt behavior described earlier, taking every other task down with it. A real "kill this one task and keep scheduling the rest" path is still future work.
- **No true preemption for a task that never blocks.** The flat round-robin queue is fair *among tasks that eventually sleep or yield*, but a hypothetical ring-0 task stuck in a tight, non-yielding loop would still be re-queued and re-run every time its turn came up on a PIT tick, since the current switch only happens at tick boundaries — there's no forced timeslice cutoff mid-execution beyond that.
- Each ring-3 task still shares the *same* page directory as the kernel and every other task — there's still no per-process address space isolation, same caveat as the usermode and ELF-loader sections. Multitasking here means multiple independent streams of execution, not multiple protected processes.

## Roadmap

1. ~~Paging / virtual memory~~ — done (v0.2.0)
2. ~~Heap (`kmalloc`/`kfree`)~~ — done (v0.3.0)
3. ~~Serial driver~~ — done (v0.3.1)
4. ~~Ring 0 → Ring 3 (usermode + syscalls)~~ — done (v0.4.0)
5. ~~Disk driver (ATA PIO) + flat filesystem~~ — done (v0.5.0)
6. ~~ELF loader~~ — done (v0.6.0)
7. ~~Multitasking (round-robin, mixed ring0/ring3, sleep)~~ — done (v0.7.0)
8. A way to kill a single faulting task instead of halting the whole kernel
9. Fair preemption for a hypothetical never-blocking task (currently only switches at tick boundaries between otherwise-cooperative tasks)

## Building

```bash
cd GabutOS
make
make run   # requires qemu-system-i386
```

Requires `nasm`, an `i686-elf-gcc` cross-compiler, and `qemu-system-i386`. A secondary `Makefile.tested-with-gcc-m32` is included as a fallback for environments without the cross-compiler (uses host `gcc -m32` plus a few extra linker flags to compensate for differences from a true `i686-elf` toolchain).
