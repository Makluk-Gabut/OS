; Konstanta standar untuk Multiboot Header (wajib ada supaya GRUB mau load kernel kita)
MBALIGN  equ  1 << 0            ; Align modules on page boundaries
MEMINFO  equ  1 << 1            ; Minta bootloader ngasih info map memory
FLAGS    equ  MBALIGN | MEMINFO
MAGIC    equ  0x1BADB002        ; Magic number agar GRUB mengenali ini sebagai kernel
CHECKSUM equ -(MAGIC + FLAGS)   ; Checksum matematis

; --- MULTIBOOT HEADER ---
section .multiboot
align 4
    dd MAGIC
    dd FLAGS
    dd CHECKSUM

; --- STACK SETUP ---
section .bss
align 16                ; x86 C ABI butuh 16-byte stack alignment
stack_bottom:
resb 16384              ; Alokasikan 16 KiB untuk stack (cukup untuk kernel dasar)
stack_top:

; --- ENTRY POINT ---
section .text
global _start:function (_start.end - _start)
_start:
    ; 1. Inisialisasi Stack Pointer
    mov esp, stack_top

    ; 2. Interrupt tetap mati dulu di sini (default CPU state).
    ;    Ini AMAN karena kernel_main() yang akan pasang GDT+IDT+PIC dulu
    ;    baru manggil `sti` sendiri setelah semuanya siap.
    cli

    ; 3. Lompat ke kode C, teruskan info dari GRUB:
    ;    EAX = multiboot magic (0x2BADB002), EBX = pointer ke struct multiboot_info
    ;    cdecl: push argumen dari kanan ke kiri, jadi ebx (arg2) dulu baru eax (arg1)
    push ebx
    push eax
    extern kernel_main
    call kernel_main
    add esp, 8

    ; 4. Fallback Hang Loop (Safety Net)
    ; Seharusnya tidak pernah sampai sini karena shell() infinite loop.
.hang:
    cli
    hlt
    jmp .hang
.end:
