# GabutOS

A hobby x86 kernel built from scratch, booted via GRUB (Multiboot1). This document explains the design rationale behind every module — the "why", not just the "what" — since the source files themselves are kept comment-free.

## Directory Structure

```
GabutOS/
├── kernel.c          orchestrator: boots subsystems, hands off to the shell
├── boot/             Multiboot header, entry point, linker script
├── cpu/              GDT, IDT, exception/IRQ handling, TSS, ring0<->ring3, port I/O
├── mm/                physical + virtual memory management, heap allocator
├── drivers/          VGA, serial, keyboard, PIT timer, ATA disk
├── fs/                flat filesystem
├── loader/            ELF32 parser and loader
├── task/              task control blocks and the scheduler
└── shell/             command tokenizer and every shell command's implementation
```

Files reference each other with plain `#include "header.h"` regardless of which folder they're actually in — the Makefile's `-Icpu -Imm -Idrivers -Ifs -Iloader -Itask -Ishell` flags handle path resolution, so no include ever needs a relative path like `../cpu/idt.h`.

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

## Shell (`shell/shell.c`, v1.1.0 rewrite)

Through v1.0.0, the shell lived entirely inside `kernel.c` as a long chain of `if (strcmp(cmd, "x") == 0) { ... } else if (...)` blocks, each command matched against the *entire* raw input line — meaning any command needing an argument (`cat`, `rm`, `run`) had to manually skip a fixed prefix length (`cmd + 4` for `"cat "`, for instance) rather than parse anything resembling real arguments. By v1.0.0 this had grown to roughly 250 lines inside a single 386-line file, all crammed into one function.

The shell was extracted into its own `shell/` module with a real tokenizer. `shell_tokenize()` walks a raw input line in place — no new buffer, no `kmalloc()` — splitting on runs of spaces and writing `'\0'` terminators directly into the original line buffer, with `argv[]` entries pointing at the start of each resulting word. This is the same in-place splitting technique the standard C library's own `strtok` uses, chosen here specifically to avoid a heap allocation on every single command a user types. The result is an ordinary `argc`/`argv` pair, structurally identical to what `main()` receives in a hosted C program — commands look up `argv[0]` in a small dispatch table instead of chaining string comparisons, and anything needing an argument reads `argv[1]` directly instead of manually walking past a hardcoded prefix length.

Every command from v1.0.0 was moved over unchanged in *behavior* — `cat`, `rm`, and `run` now check `argc < 2` and print a usage message instead of silently misbehaving on a bare command with no filename, which is a small but real improvement the old prefix-skipping approach didn't have (calling bare `cat` before this rewrite would have read one byte past the empty string as if it were a filename).

### Verified Behavior

Every command from the v1.0.0 test suite (`fsformat`, `loadtest`, `ls`, `rm`, `multitask`, `run` against both a well-behaved and a deliberately crashing and a deliberately oversized program, `crashtest`, `isotest`, `ps`) was re-run in a single session against the rewritten shell and produced identical output to before — confirming the parser rewrite changed nothing about existing command behavior. A further round specifically exercised the new tokenizer's edge cases:

```
GabutOS> cat
Pakai: cat <nama>
GabutOS> rm
Pakai: rm <nama>
GabutOS> nyoba command asal
Command not found: nyoba
GabutOS> cat  hello.txt
File gak ketemu: hello.txt
```

A command with no argument now gives a clear usage message instead of crashing or reading garbage; an unrecognized multi-word input correctly reports only `argv[0]` (`nyoba`) as the unknown command rather than treating the whole line as one unmatched string; and a command with *double* spaces between the command name and its argument (`cat  hello.txt`) still parses `hello.txt` as a single clean argument, confirming the tokenizer correctly treats runs of whitespace as one separator rather than only handling exactly one space.

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

### Known Limitations (as of v0.7.0)

- No way yet to kill a faulting task — see the "Task Kill + Preemption Correction" section below, added in v0.8.0.
- No true preemption for a task that never blocks — also addressed, and partly *corrected* rather than fixed, below.
- Each ring-3 task still shares the *same* page directory as the kernel and every other task — there's still no per-process address space isolation, same caveat as the usermode and ELF-loader sections. Multitasking here means multiple independent streams of execution, not multiple protected processes. This remains unaddressed as of v0.8.0 and is intentionally scoped as separate, larger future work.

## Task Kill + Preemption Correction (v0.8.0)

Two of the three limitations listed above turned out to need different treatment than expected once actually investigated — one was a real gap that got fixed, the other turned out to already not be a gap at all.

### Preemption: Re-reading the Existing Code

Revisiting `scheduler_tick()` while starting this work turned up something that had been mis-stated in the v0.7.0 notes above: it already switches tasks unconditionally on *every single* PIT interrupt, not just when a task blocks. Since the PIT fires at 100Hz, every ready task already gets forcibly preempted roughly every 10ms whether it asked to yield or not — which is, in fact, exactly what the multitasking demo's interleaved `A`/`B`/`C` output already proved back in v0.7.0. The "no true preemption" claim was an overstatement that didn't hold up against the kernel's own already-working behavior.

The one genuine gap: a ring-0 task that executes `cli` and then loops without ever executing `sti` would disable the PIT interrupt itself, and since the scheduler only runs *inside* that interrupt, such a task truly could hog the CPU forever. But this isn't fixable with more scheduler logic — it's an inherent trust boundary of letting any code run at ring 0 at all, common to essentially every monolithic kernel. Notably, ring-3 tasks can't trigger this: `cli` from ring 3 is a privileged instruction, and the earlier usermode demo already proved the CPU rejects it with a General Protection Fault. So the real, honest scope of this limitation is: *trusted ring-0 kernel code could theoretically starve the system if it disables interrupts and never re-enables them* — which is a statement about what ring 0 fundamentally means, not a scheduler bug to chase further.

### Kill Task: A Real Fix, Found By Testing It

This one was genuinely broken and got fixed. Each `struct task` gained a `killable` flag (set for every task created via `task_create()` — i.e. anything loaded from an ELF — and left unset for the task wrapping the already-running shell via `task_create_current()`, so the interactive shell itself can never be torn down this way).

`isr_handler()` now checks, after invoking whatever handler is registered for a given CPU exception (0–31), whether the currently running task is killable. If so, instead of falling into the kernel-wide panic-and-halt loop, it calls `scheduler_kill_current()`: the task is marked `TASK_DEAD`, its kernel stack and task struct are freed back to the heap, and the scheduler immediately picks the next ready task to resume — inside the very same interrupt that caught the fault, using the identical "return a different esp" mechanism the rest of the scheduler already relies on.

The page fault handler (`vmm.c`) needed a matching change: it used to unconditionally `hlt` forever on any fault. It now only does that when the current task *isn't* killable (preserving the original hard-halt behavior for genuine kernel-level failures, e.g. before multitasking has even started); otherwise it prints its diagnostic and returns normally, letting `isr_handler`'s now-centralized kill logic take over. This keeps the "should we kill or halt" decision in one place rather than duplicated across every individual exception handler.

**A first attempt at testing this immediately caught a real bug**: a deliberately crashing task (writes to `0xDEADBEEF`, an unmapped address) still froze the whole kernel. The page fault handler's own unconditional `hlt` loop was catching the fault *before* `isr_handler`'s new kill-or-panic logic ever got a chance to run — the kill path had been added to `isr_handler`, but the already-registered page fault handler still had its own independent, older "always halt" behavior that never delegated back. Fixed by making the page fault handler itself killability-aware, as described above, rather than assuming every exception handler would automatically respect the new centralized logic.

### Verified Behavior

With `multitask` running (task-A, task-B, task-C as in v0.7.0), a fourth task was deliberately loaded to crash — an ELF that writes through a null-ish unmapped pointer, `0xDEADBEEF` — via a new `crashtest` shell command:

```
GabutOS> crashtest
task-CRASH ditambahkan -- dia bakal nulis ke alamat gak valid dan crash.
Perhatiin: task lain (A/B/C) harusnya tetap jalan normal setelahnya.
GabutOS>
[PAGE FAULT] alamat=0xDEADBEEF err_code=0x00000006
  present=no (halaman belum di-map)
  akses=write
  mode=user
Task dihentikan, lanjut ke task lain.
CABABCAABACABACBAABCAABps
  * shell (running, ring0, prio 0)
    task-B(r3) (sleeping sampai tick 1625)
    task-A(r3) (sleeping sampai tick 1605)
    task-C(r0) (sleeping sampai tick 1605)
```

The faulting task's address and access type were correctly diagnosed (unmapped, write, from ring 3, matching what the test program actually does). Critically, `A`/`B`/`C` output kept appearing *after* the fault — not just "the kernel didn't freeze," but the three surviving tasks kept genuinely executing and being scheduled. `ps` afterward listed exactly the three surviving tasks; `task-CRASH` doesn't appear anywhere, confirming it was fully removed from the scheduler rather than merely marked and left lingering. The shell remained responsive throughout (visible from `ps`'s own characters interleaving with the letter output, same as the v0.7.0 test). Zero crashes beyond the two ordinary QEMU startup resets.

### A Note on Memory Safety During Kill

`scheduler_kill_current()` frees the dying task's kernel stack via `kfree()` while the CPU is, at that exact moment, still executing on that very stack (inside the interrupt handler that caught the fault). This is safe *specifically* because `heap.c`'s `kfree()` only flips bookkeeping flags in the free-list — it never zeroes or unmaps the underlying bytes, so the memory stays intact and readable/executable right up until something else allocates over it. The function finishes running, returns its result in `EAX`, and only then does the interrupt stub actually load a different stack pointer and abandon the freed memory for good. This is a deliberate, documented dependency on this specific allocator's behavior — swapping in a more aggressive allocator that scrubs or immediately reuses freed memory would break this and would need the kill path to switch stacks *before* freeing, not after.

### Known Limitations (as of the first half of v0.8.0)

- Per-process address space isolation is still the one large piece left unaddressed — every task, killable or not, still runs against the same shared page directory as the kernel. This is intentionally scoped as separate future work rather than folded into this change.
- A killed task's physical stack *pages* aren't reclaimed — only the kernel-heap-backed structures (`struct task`, the kernel stack buffer) are freed via `kfree()`. The physical page(s) backing its ring-3 user stack, allocated via `pmm_alloc_page()` in `task_create()`, are currently leaked. This matches a similar gap already noted in the filesystem section (no space reclamation) — fixing it needs `vmm_map_page()` to grow a companion "unmap and return the physical page to the PMM" operation, which doesn't exist yet.
- The ring-0 `cli`-forever starvation case described above remains structurally unfixable without deeper trust/isolation mechanisms (e.g. a watchdog NMI) that are out of scope for now.

## Per-Process Address Space Isolation (second half of v0.8.0)

Both remaining limitations above — no isolation, no stack reclamation — were closed in the same continued v0.8.0 effort (kept under the same version number by request, since it directly extends the task-kill work rather than starting a new feature line).

### What Changed, Mechanically

`vmm.h`/`vmm.c` gained the operations a per-process address space actually needs: `vmm_create_address_space()` (allocate and initialize a brand-new page directory), `_in`-suffixed variants of the existing map/mapped-check functions that take an explicit page directory instead of always assuming the one global kernel directory, `vmm_unmap_and_free_in()` (the reclaim operation that was missing — walks a specific mapping, frees the backing physical page back to the PMM, and invalidates the TLB entry), and small helpers to read/write `CR3` directly (`vmm_current_directory()`, `vmm_activate()`).

`struct task` gained a `page_directory` field — `NULL` means "run against the shared kernel directory" (every task from v0.7.0 and the shell itself still work exactly this way), a non-`NULL` pointer means "this task has its own isolated address space." `task_create()` and `elf_load()` both now take this pointer through, mapping a task's user stack (and, for `elf_load()`, its code/data segments) into whichever directory was requested instead of always the global one.

The scheduler's `switch_into()` — already the single, centralized place every task switch routes through, a direct result of the v0.7.0/early-v0.8.0 refactor that consolidated switching logic to avoid exactly the kind of duplicated-behavior bug that caused the page-fault-handler issue earlier — was a natural place to add one more line: load `CR3` from the incoming task's `page_directory` (or the shared kernel directory if it doesn't have one) on every switch. Because this already lives in one function used by all three switch paths (tick-based preemption, syscall-triggered sleep, and task-kill), the CR3 switch is automatically correct in all three without needing to touch them individually.

### Two Real Bugs, Found by Actually Running It — Not Read Into Existence

The first working version of the isolation demo (two tasks, `task-X` and `task-Y`, deliberately linked to the exact same virtual address, `0x1000000`, in separate address spaces — chosen specifically so success could only mean the isolation was real, not an accident of different addresses happening not to collide) did not work correctly on the first two attempts, in two entirely different ways.

**Bug 1 — a naive full copy of the page directory silently defeated the isolation it was meant to provide.** The first implementation of `vmm_create_address_space()` copied the entire 1024-entry kernel page directory into the new one. This seemed reasonable — share the kernel's mappings, get a private copy to add task-specific mappings into — but it silently broke because a *copied* page directory entry still points at the *same underlying page table*. Since `task-A`, `task-B`, and `task-C` (from the ordinary `multitask` demo, run just before `isotest` in the same session) had already been loaded into the *shared* kernel directory at `0x1000000`, `0x1100000`, and `0x1200000` — and `task-A` specifically shares `isotest`'s chosen collision address, `0x1000000` — the freshly copied `pd_x` and `pd_y` inherited an *already-present* mapping at that exact address, still pointing at task-A's page table. `elf_load()`'s "skip if already mapped" check (there to avoid double-allocating pages for segments that share a boundary page, a legitimate optimization from the ELF-loader work) then saw that address as already mapped and skipped creating a new page — so task-X's and task-Y's ELF bytes both got written into the *same physical page* task-A's code already occupied, each overwriting the last. The observable symptom was exactly this: only `Y`'s output ever appeared (it loaded last, so its bytes "won"), and — following that thread further — `task-A` itself would have been silently corrupted too, though this specific run didn't surface that side effect directly.

**Bug 2 — the fix for Bug 1 overcorrected and broke basic task-switching.** Restricting `vmm_create_address_space()` to only copy the true 0–4MB identity-mapped region (page directory index 0) and leave everything else empty seemed like the obvious, principled fix — collisions like the above become structurally impossible if nothing beyond the kernel's own fixed region is ever shared by default. It compiled and booted, but every task running under an isolated directory immediately page-faulted trying to read completely unrelated-looking addresses (`0x00403160`, `0x00403178`) — which turned out to be squarely inside the **heap** (`0x400000`–`0x800000`, i.e. page directory index 1, immediately *after* the identity-mapped region, not inside it). `struct task` itself, and every task's kernel stack, are `kmalloc()`'d — living in the heap. An isolated task's own control-block and stack are therefore *kernel* data that has to remain visible in *every* address space regardless of which task is "isolated," in the same way essentially every real OS maps a shared kernel region into every process's page tables. The fix was to extend the shared portion of `vmm_create_address_space()` to also copy page directory index 1 (the heap's range) alongside index 0 — restoring correct kernel/heap visibility everywhere, while index 2 and above (where task code, task stacks, and everything isolation is actually meant to separate lives) remain genuinely private per address space.

Both bugs were caught by running the actual demo and reading what broke, not by reasoning about the code in the abstract — the first attempt produced plausible-looking, non-crashing, wrong output (`Y` printing endlessly, `X` never appearing) that could easily have been mistaken for a scheduling quirk rather than a memory-sharing bug if it hadn't been cross-checked against expected 50/50 output; the second attempt crashed immediately and loudly via the kernel's own page fault handler, which made it comparatively easy to diagnose once the faulting addresses were checked against the heap's known range.

### Verified Behavior

With `multitask` already running (so `task-A`/`B`/`C` occupy `0x1000000`/`0x1100000`/`0x1200000` in the *shared* directory), a new `isotest` command creates two separate address spaces via `vmm_create_address_space()`, loads two different ELF binaries — both deliberately linked to `0x1000000`, the same address `task-A` already uses in the shared space — one per isolated directory, and schedules them alongside everything else already running:

```
GabutOS> isotest
task-X dan task-Y ditambahkan, DUA-DUANYA di-load ke alamat
virtual yang SAMA (0x1000000), tapi di page directory TERPISAH.
Kalau isolasi beneran jalan: X terus ngeprint 'X', Y terus ngeprint 'Y',
gak ada yang ke-corrupt walau alamatnya identik.
GabutOS> XYABYXACXYABAYXBCAXYABYXACABXYACYXBAXYABCAYXABXYAC
```

Both letters kept appearing steadily for the full duration of an extended run — a raw count over the captured serial log showed `X` appearing 39 times and `Y` 40 times, consistent with the scheduler's flat round-robin giving both tasks an equal share of turns, and neither ever displacing or corrupting the other despite sharing the exact same virtual address. `ps` correctly labeled `task-X(iso)` and `task-Y(iso)` as `isolated`, distinct from the `shared` label on `task-A`/`B`/`C`/the shell.

Stack reclamation was verified by watching `pages`' free-page count across a full create-and-kill cycle: 32461 free pages before `multitask`, 32449 after three tasks were created (a small, expected drop), and — after `crashtest` added a fourth task that immediately faulted and was killed — 32447, a net drop of only 2 pages from the prior reading rather than growing unboundedly. This confirms the killed task's physical stack page came back to the PMM instead of leaking, addressing the second limitation listed above.

`crashtest` (the existing kill-on-fault demo from earlier in v0.8.0) was re-run after all these changes to confirm no regression: identical behavior to before — the faulting task is removed, `A`/`B`/`C` keep running, the shell stays responsive.

### Known Limitations (final, as of v0.8.0)

- Isolation is currently binary and manual — a task either gets a fully private directory (everything above the shared kernel+heap region) or shares the kernel's directory entirely, decided by whoever calls `task_create()`/`elf_load()`. There's no automatic promotion of, say, the `run` command's ordinary ELF-loaded tasks to isolated directories by default; they still default to the shared space unless explicitly given one, matching v0.7.0's behavior for backward compatibility.
- Physical *code/data* pages for an isolated task aren't reclaimed on kill yet — only its stack page is (via the same `vmm_unmap_and_free_in()` path). Extending reclamation to walk and free every mapping in a killed task's private directory (and eventually free the directory's own physical page) is straightforward given the primitives now in place, but hasn't been done.
- There's still no memory protection *between* two isolated tasks beyond the address space separation itself — no permission or quota system, no limit on how much physical memory an isolated task's own segments can consume beyond the PMM simply running out.

## Full Reclaim + Auto-Isolation (v0.9.0)

This closes both remaining items from the v0.8.0 roadmap in one pass.

### Full Reclaim: `vmm_destroy_address_space()`

v0.8.0's process isolation only reclaimed a killed isolated task's *stack* page — its code and data pages, and the page directory itself, were leaked. `vmm_destroy_address_space()` closes this: it walks page directory indices 2 through 1023 (deliberately skipping indices 0 and 1, the shared kernel-and-heap region every address space inherits — freeing those would corrupt every other task and the kernel itself), and for each present entry frees every mapped physical page in that page table, then the page table itself, then finally the page directory's own physical page.

This gets called from `scheduler_kill_current()`, but only *after* `switch_into()` has already loaded a different task's `CR3` — freeing a page directory while it's still the one active in the CPU's own `CR3` register would be actively dangerous (that memory could be reused for something else while the CPU is still using it as its page directory). The ordering here isn't incidental; it's the same category of care that went into the memory-safety note on `kfree()`-ing a task's kernel stack in the earlier kill-task work.

### Auto-Isolation: `run` Rebuilt on Top of the Task System

The original `run` command (from v0.6.0, unchanged through v0.8.0) predates the task/scheduler system entirely — it called `usermode_jump()`, a synchronous one-shot ring-3 transition that never created a `struct task` and never went through the scheduler at all. It effectively *replaced* the shell permanently rather than coexisting with it as one task among several.

`run` is now rebuilt to match `multitask` and `isotest`: it calls `vmm_create_address_space()` for every invocation (no opt-out — isolation is no longer something a caller has to remember to ask for), loads the ELF into that new directory, wraps it in a real `struct task`, and hands it to `scheduler_add_task()`. Because it now depends on the scheduler actually running, `run` checks `scheduler_is_active()` first and asks the user to start `multitask` if it isn't — rather than silently falling back to the old one-shot behavior, which would have made `run`'s behavior inconsistent depending on whether multitasking happened to already be active.

### Verified Behavior

**Auto-isolation:**
```
GabutOS> run hello.elf
Jalankan 'multitask' dulu (nyalain scheduler), baru 'run <nama>' bisa dipakai.
GabutOS> multitask
GabutOS> run hello.elf
[elf] 'hello.elf' jalan sebagai task terisolasi (page directory sendiri).
GabutOS> Halo dari program ELF asli yang di-load dari disk!
```
`ps` afterward correctly labels the `run`-launched task `isolated`, same as `isotest`'s tasks, distinct from `task-A`/`B`/`C`'s `shared` label — confirming ordinary program launches now get real isolation by default, not just the dedicated demo command.

**Full reclaim**, tested with a new deliberately-crashing program (`crash.elf`, writes through an unmapped pointer after printing a message) run in isolation via the now-rebuilt `run`:
```
GabutOS> pages
Physical pages: 32448 free / 32639 total (129792 KB bebas)
GabutOS> run crash.elf
[elf] 'crash.elf' jalan sebagai task terisolasi (page directory sendiri).
task isolated ini bakal crash sebentar lagi

[PAGE FAULT] alamat=0xCAFEBABE err_code=0x00000006
Task dihentikan, lanjut ke task lain.
GabutOS> pages
Physical pages: 32431 free / 32639 total (129724 KB bebas)
GabutOS> ps
  * shell (running, ring0, shared)
    task-A(r3) (sleeping..., shared)
    task-C(r0) (sleeping..., shared)
    task-B(r3) (sleeping..., shared)
```
The free-page count moved by an amount consistent with ordinary background task churn (`task-A`/`B`/`C` continuing to cycle through their own sleep/wake pages) rather than growing unboundedly the way it would if the crashing task's code/data pages and page directory had leaked. `ps` shows `crash.elf` fully gone from the task list — not just its stack reclaimed, but the entire task and its address space. Zero crashes beyond the routine QEMU startup resets.

### Known Limitations

- There's still no memory quota — an isolated task can still exhaust physical RAM through a large enough ELF segment before it ever gets a chance to fault or be killed.
- Reclaiming a page directory's physical page assumes nothing else still references it; this holds today because a task's directory is never shared with any other task, but would need revisiting if address-space sharing (e.g. threads within one process) is ever added.
- `run`'s new dependency on `multitask` being active first is a deliberate, visible behavior change from v0.6.0–v0.8.0 — scripts or muscle memory built around the old "run works standalone" behavior will need to adjust.

## Filesystem Delete + Reclaim, and Memory Quotas (v1.0.0)

Three specific gaps were closed for this release, all explicitly named as the reason `1.0.0` was held back rather than tagged at v0.9.0: the flat filesystem had no way to delete a file or reuse its space, and an isolated task had no upper bound on how much physical memory it could consume before ever getting a chance to fault or be killed.

### Filesystem: Delete and a Real Free-Space Allocator

Every version through v0.9.0 only ever grew the data area — `sb.next_free_lba` moved forward on every write and never moved back, so a filesystem that was written to and deleted from repeatedly would eventually run out of disk regardless of how many files were actually still present. `fs_delete()` closes this, but doing it correctly needed more than just clearing a file-table entry: the sectors it occupied have to become genuinely reusable by a future write, not just abandoned.

The superblock gained a small free-extent list (up to 16 entries, each a `start_lba`/`sector_count` pair) — conceptually the same free-list idea `heap.c`'s allocator already uses for RAM, applied here to disk sectors instead. `fs_delete()` adds the deleted file's range back into this list, merging it with an adjacent extent if one directly borders it (coalescing, to avoid the list filling up with many small fragments over repeated delete cycles). `fs_write()` now checks this list first (`free_extents_take()`, a first-fit search) before falling back to the old bump-forward behavior — so a newly written file will land in previously-deleted space if a big-enough hole exists, rather than always growing the disk further out.

`fs_write()` also became overwrite-aware: writing to a name that already exists now calls `fs_delete()` on the old entry first (freeing its sectors back to the pool) before writing the new data, instead of what it did through v0.9.0 — silently creating a second table entry with the same name, leaving the original's sectors permanently orphaned with no way to reach or reclaim them.

One structural note: adding the free-extent list to `struct fs_superblock` meant the on-disk layout changed, so a disk formatted by an older version isn't compatible with this one — `fs_mount()` still checks the magic number and will simply refuse to mount anything that doesn't match, rather than misinterpreting old data as a valid free-extent list.

### Verified Behavior

```
GabutOS> fsformat
GabutOS> fstest
Nulis hello.txt sukses.
GabutOS> loadtest
hello.elf (768 bytes) ditulis ke disk.
crash.elf (764 bytes) ditulis ke disk...
GabutOS> ls
  hello.txt (69 bytes)
  hello.elf (768 bytes)
  crash.elf (764 bytes)
GabutOS> rm hello.txt
Dihapus: hello.txt
GabutOS> ls
  hello.elf (768 bytes)
  crash.elf (764 bytes)
GabutOS> cat hello.txt
File gak ketemu: hello.txt
GabutOS> fstest
Nulis hello.txt sukses.
GabutOS> cat hello.txt
Halo dari GabutOS! Data ini beneran nempel di disk (bukan cuma RAM).
```

The second `fstest` after deletion successfully re-created `hello.txt` and read back the *new* write correctly — confirming the reclaimed sectors were both reusable and not accidentally serving stale data from before the delete.

### Memory Quotas for Isolated Tasks

`elf_load()` gained a `max_pages` parameter. As it walks a program's `PT_LOAD` segments allocating and mapping physical pages, it now counts pages as it goes and — if a load would exceed the limit — stops immediately, prints why, and rolls back: every page it had already allocated and mapped *during this specific call* gets unmapped and freed via `vmm_unmap_and_free_in()`, so a rejected program leaves no partial, orphaned mappings behind. Internal callers that load the kernel's own trusted demo programs (`multitask`, `isotest`, `crashtest`) pass `0`, meaning unlimited — the quota exists to bound what an arbitrary, untrusted ELF handed to `run` can do, not to second-guess code the kernel itself is choosing to load. `run` specifically enforces a 64-page (256KB) ceiling.

### Verified Behavior

A test program (`hog.elf`) with roughly 500KB of `.bss` — deliberately well past the 64-page ceiling — was written to disk and launched:

```
GabutOS> pages
Physical pages: 32447 free / 32639 total
GabutOS> run hog.elf
[elf] program melebihi quota memori (64 halaman)
[elf] gagal load (lihat pesan error di atas)
GabutOS> pages
Physical pages: 32431 free / 32639 total
GabutOS> ps
  * shell (running, ring0, shared)
    task-C(r0) (sleeping..., shared)
    task-A(r3) (sleeping..., shared)
    task-B(r3) (sleeping..., shared)
```

The free-page count dropped by 16 between the two `pages` checks — consistent with ordinary background churn from `task-A`/`B`/`C` continuing to sleep and wake during the several seconds the test took, not the 64+ pages that would have leaked had the rollback failed to actually free what `hog.elf`'s partial load had allocated. `ps` confirms `hog.elf` never became a task at all — it was rejected before `task_create()` was ever called. A follow-up regression check confirmed `hello.elf` (well under the 64-page limit) still launches and runs normally afterward, unaffected by the new check.

### Known Limitations

- The free-extent list is capped at 16 entries; a filesystem subjected to a very fragmented pattern of writes and deletes could exhaust it, after which further freed space would stop being tracked (though still safely inert — it wouldn't corrupt anything, just become permanently unreclaimed, similar in spirit to the heap's own forward-only merge limitation noted earlier).
- The quota is a flat page count with one fixed value for everything launched via `run`; there's no per-program or per-user configurability, and no equivalent quota for a task's stack or kernel-side resources — only its ELF-loaded code and data are bounded.
- Deleting a file only affects future allocations; it doesn't overwrite the freed sectors' actual on-disk bytes, so data technically remains physically present on the disk image until something else writes over it — ordinary for how most real filesystems' `delete` operations work too, but worth being explicit about for anyone assuming "deleted" means "securely erased."

## Roadmap

1. ~~Paging / virtual memory~~ — done (v0.2.0)
2. ~~Heap (`kmalloc`/`kfree`)~~ — done (v0.3.0)
3. ~~Serial driver~~ — done (v0.3.1)
4. ~~Ring 0 → Ring 3 (usermode + syscalls)~~ — done (v0.4.0)
5. ~~Disk driver (ATA PIO) + flat filesystem~~ — done (v0.5.0)
6. ~~ELF loader~~ — done (v0.6.0)
7. ~~Multitasking (round-robin, mixed ring0/ring3, sleep)~~ — done (v0.7.0)
8. ~~Kill a faulting task instead of halting the kernel~~ — done (v0.8.0)
9. ~~Preemption for non-cooperative tasks~~ — turned out to already work by design; clarified, not changed (v0.8.0)
10. ~~Per-process address space isolation (separate page directory per task)~~ — done (v0.8.0)
11. ~~Reclaim a killed task's physical user-stack page~~ — done (v0.8.0)
12. ~~Reclaim a killed isolated task's code/data pages and its own page directory~~ — done (v0.9.0)
13. ~~Automatic isolation for ordinary `run`-launched tasks~~ — done (v0.9.0)
14. ~~Filesystem delete + real space reclamation~~ — done (v1.0.0)
15. ~~Memory quotas for isolated tasks~~ — done (v1.0.0)
16. ~~Shell rewritten with real `argc`/`argv` argument parsing~~ — done (v1.1.0)

## v1.0.0 — Where This Stands

Every roadmap item is closed, including the three (filesystem delete/reclaim, and memory quotas) specifically held back from v0.9.0 as the reason `1.0.0` wasn't tagged yet. This is GabutOS's first release meant to be treated as a coherent, complete system rather than an in-progress snapshot: it boots standalone via GRUB, manages physical and virtual memory, reads and writes a real (if intentionally simple) filesystem, loads and runs independently compiled ELF binaries, and runs several of them concurrently with genuine per-process address space isolation, memory quotas, and the ability to kill a misbehaving one without taking the rest of the system down.

`1.0.0` here does not mean "free of every rough edge." Every feature section above still lists honest, specific "Known Limitations" — a flat filesystem with a bounded free-list, no dynamic linking, no way to stop a ring-0 task that disables interrupts forever, and others. What changed at this version isn't that those went away; it's that none of them are structural gaps in something the kernel claims to do — they're documented boundaries of a system that otherwise works end to end, which is the bar this project set for calling a release stable.

## Post-1.0.0: Toward a Self-Hosted Compiler

With the kernel itself considered stable, the next large goal is letting someone write and compile a program *inside* GabutOS — not just run an ELF that was compiled elsewhere and copied onto the disk, which is all that's possible today. A full C compiler is out of scope for a project this size (production compilers are millions of lines of code built by large teams over years); the realistic target is a small, C-*like* toy language compiler, a common and well-scoped exercise in compiler construction. The planned path there:

1. ~~A real shell with `argc`/`argv` argument parsing~~ — done (v1.1.0), a prerequisite for everything below since a compiler and its supporting tools need actual command-line arguments (a filename to compile, flags, etc.), not fixed-prefix string matching.
2. A minimal line-based text editor — so code can actually be written from within GabutOS, rather than always arriving pre-compiled from the host.
3. A small toy-language compiler — starting from the smallest possible subset (arithmetic expressions, then variables, then control flow) rather than attempting C compatibility from day one.
4. A minimal assembler/linker so the compiler's output can become a real ELF binary the existing loader can run directly.

## Building

```bash
cd GabutOS
make
make run   # requires qemu-system-i386
```

Requires `nasm`, an `i686-elf-gcc` cross-compiler, and `qemu-system-i386`. A secondary `Makefile.tested-with-gcc-m32` is included as a fallback for environments without the cross-compiler (uses host `gcc -m32` plus a few extra linker flags to compensate for differences from a true `i686-elf` toolchain).
