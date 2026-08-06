global enter_usermode
enter_usermode:
    mov ecx, [esp+4]
    mov edx, [esp+8]

    mov ax, 0x23
    mov ds, ax
    mov es, ax
    mov fs, ax
    mov gs, ax

    push dword 0x23
    push edx
    pushf
    pop eax
    or eax, 0x200
    push eax
    push dword 0x1B
    push ecx
    iret
