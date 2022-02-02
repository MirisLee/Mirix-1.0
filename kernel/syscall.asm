; Mirix 1.0/kernel/syscall.asm
; (C) 2022 Miris Lee

SIGCHLD     equ 17

_EAX        equ 0x00
_EBX        equ 0x04
_ECX        equ 0x08
_EDX        equ 0x0c
_FS         equ 0x10
_ES         equ 0x14
_DS         equ 0x18
_EIP        equ 0x1c
_CS         equ 0x20
_EFLAGS     equ 0x24
_OLD_ESP    equ 0x28
_OLD_SS     equ 0x2c

; offset within task_struct
state       equ 0
counter     equ 4
priority    equ 8
signal      equ 12
sigaction   equ 16
blocked     equ (33*16)

; offset within sigaction
sa_handler  equ 0
sa_mask     equ 4
sa_flags    equ 8
sa_restorer equ 12

nr_syscalls equ 72

global _system_call, _sys_fork, _sys_execve
global _hd_int, _floppy_int
global _device_not_available, _coprocessor_error, _timer_interrupt
extern _schedule, _syscall_table
extern _current, _task, _do_signal, _jiffies, _do_timer
extern _find_empty_process, _copy_process, _do_execve

align 2
bad_system_call:
    mov eax, -1
    iret

align 2
reschedule:
    push ret_from_system_call
    jmp _schedule

align 2
_system_call:
    cmp eax, nr_syscalls-1
    ja bad_system_call
    push ds
    push es
    push fs
    push edx
    push ecx
    push ebx
    mov edx, 0x10
    mov ds, dx
    mov es, dx
    mov edx, 0x17
    mov fs, dx
    call _syscall_table+eax*4
    push eax
    mov eax, _current
    cmp dword [eax+state], 0
    jne reschedule
    cmp dword [eax+counter], 0
    je reschedule
ret_from_system_call:
    mov eax, _current
    cmp eax, _task      ; task[0]
    je signal_end
    cmp word [esp+_CS], 0x0f
    jne signal_end
    cmp word [esp+_OLD_SS], 0x17
    jne signal_end
    mov ebx, dword [eax+signal]
    mov ecx, dword [eax+blocked]
    not ecx
    and ecx, ebx        ; allowed signal bitmap
    bsf ecx, ecx
    je signal_end
    btr ebx, ecx
    mov dword [eax+signal], ebx
    inc ecx
    push ecx
    call _do_signal     ; kernel/signal.c
    pop eax
signal_end:
    pop eax
    pop ebx
    pop ecx
    pop edx
    pop fs
    pop es
    pop ds
    iret

align 2
_timer_interrupt:
    push ds
    push es
    push fs
    push edx
    push ecx
    push ebx
    push eax
    mov eax, 0x10
    mov ds, ax
    mov es, ax
    mov eax, 0x17
    mov fs, ax
    inc _jiffies
    mov al, 0x20       ; EOI
    out 0x20, al
    mov eax, dword [esp+_CS]
    and eax, 3          ; CPL
    push eax
    call _do_timer
    add esp, 4
    jmp ret_from_system_call

align 2
_sys_fork:
    call _find_empty_process    ; kernel/fork.c
    test eax, eax
    js fork_end
    push gs
    push esi
    push edi
    push ebp
    push eax
    call _copy_process      ; kernel/fork.c
    add esp, 20
fork_end:
    ret

align 2
_sys_execve:
    lea eax, [esp+_EIP]
    push eax
    call _do_execve         ; fs/exec.c
    add esp, 4
    ret

align 2
_hd_int:
    push eax
    push ecx
    push edx
    push ds
    push es
    push fs
    mov eax, 0x10
    mov ds, ax
    mov es, ax
    mov eax, 0x17
    mov fs, ax
    mov al, 0x20    ; EOI
    out 0xa0, al
    db 0xeb, 0xeb
    xor edx, edx
    xchg edx, [_do_hd]
    test edx, edx
    jne normal_hd_int
    mov eax, _unexpected_hd_int
normal_hd_int:
    out 0x20, al
    call edx
    pop fs
    pop es
    pop ds
    pop edx
    pop ecx
    pop eax
    iret

align 2
_floppy_int:
    push eax
    push ecx
    push edx
    push ds
    push es
    push fs
    mov eax, 0x10
    mov ds, ax
    mov es, ax
    mov eax, 0x17
    mov fs, ax
    mov al, 0x20    ; EOI
    out 0xa0, al
    db 0xeb, 0xeb
    xor edx, edx
    xchg edx, [_do_floppy]
    test edx, edx
    jne normal_floppy_int
    mov eax, _unexpected_floppy_int
normal_floppy_int:
    out 0x20, al
    call edx
    pop fs
    pop es
    pop ds
    pop edx
    pop ecx
    pop eax
    iret