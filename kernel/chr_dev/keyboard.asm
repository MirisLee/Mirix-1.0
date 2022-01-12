; Mirix 1.0/kernel/chr_dev/keyboard.asm
; (C) 2022 Miris Lee

global _keyboard_int
extern _do_tty_int, _table_list, _show_stat

buf_size    equ 1024
head        equ 4
tail        equ 8
proc_list   equ 12
buf         equ 16

mode    db 0        ; caps/alt/ctrl/shift
leds    db 2        ; caps-lock/num-lock/scroll-lock
e0      db 0        ; 0xe0/0xe1

_keyboard_int:
    push eax
    push ebx
    push ecx
    push edx
    push ds
    push es
    mov eax, 0x10   ; data segment
    mov ds, ax
    mov es, ax
    xor al, al
    in al, 0x60     ; scan code
    cmp al, 0xe0
    je set_e0
    cmp al, 0xe1
    je set_e1
    call key_table+eax*4
    mov byte [e0], 0
e0_e1:
    in al, 0x61
    or al, 0x80     ; set bit 7
    out 0x61, al    ; set PPI PB7
    db 0xeb, 0xeb   ; jmp $+2, jmp $+2
    and al, 0x7f    ; clear bit 7
    out 0x61, al    ; clear PPI PB7
    mov al, 0x20    ; EOI
    out 0x20, al
    push 0
    call _do_tty_int
    add esp, 4
    pop es
    pop ds
    pop edx
    pop ecx 
    pop ebx 
    pop eax 
    iret

set_e0:
    mov byte [e0], 1
    jmp e0_e1
set_e1:
    mov byte [e0], 2
    jmp e0_e1

put_queue:
    push ecx
    push edx
    mov edx, _table_list    ; read_queue for console
    mov dword ecx, [edx+head]
char_loop:
    mov byte [edx+ecx+buf], al
    inc ecx
    and ecx, buf_size-1
    cmp dword ecx, [edx+tail]   ; buffer full
    je buf_full
    shrd eax, ebx, 8
    je no_char
    shr ebx, 8
    jmp char_loop
no_char:
    mov dword [edx+head], ecx
    mov dword ecx, [edx+proc_list]
    test ecx, ecx
    je buf_full
    mov dword [ecx], 0
buf_full:
    pop edx
    pop ecx
    ret

ctrl:
    mov al, 0x04
    jmp set_bit
alt:
    mov al, 0x10
set_bit:
    cmp byte [e0], 0
    je set_mode
    add al, al      ; right ctrl/alt pressed
set_mode:
    or byte [mode], al
    ret

unctrl:
    mov al, 0x04
    jmp clear_bit
unalt:
    mov al, 0x10
clear_bit:
    cmp byte [e0], 0
    je clear_mode
    add al, al      ; right ctrl/alt released
clear_mode:
    not al
    and byte [mode], al
    ret

lshift:
    or byte [mode], 0x01
    ret
rshift:
    or byte [mode], 0x02
    ret
unlshift:
    and byte [mode], 0xfe
    ret
unrshift:
    and byte [mode], 0xfd
    ret

caps:
    test byte [mode], 0x80
    jne return
    xor byte [leds], 0x04
    xor byte [mode], 0x40
    or byte [mode], 0x80
set_leds:
    call key_wait
    mov al, 0xed
    out 0x60, al    ; set leds command
    call key_wait
    mov byte al, [leds]
    out 0x60, al
return:
    ret
scroll:
    xor byte [leds], 0x01
    jmp set_leds
num:
    xor byte [leds], 0x02
    jmp set_leds

uncaps:
    add byte [mode], 0x7f
    ret

cursor:
    sub al, 0x47    ; numeric keypad
    jb return
    cmp al, 0x0c    ; 0x47 + 0x0c = 0x53
    ja return
    jne cur2        ; al == 12 -- del
    test byte [mode], 0x0c
    jne cur2
    test byte [mode], 0x30
    jne reboot
cur2:
    cmp byte [e0], 0x01
    je cur
    test byte [leds], 0x02
    je cur
    test byte [mode], 0x03
    jne cur
    xor ebx, ebx
    mov byte al, [num_table+eax]
    jmp put_queue
    ret
cur:
    mov byte al, [cur_table+eax]
    cmp al, '9'
    ja ok_cur
    mov ah, '~'
ok_cur:
    shl eax, 16
    mov ax, 0x5b1b  ; esc '['
    xor ebx. ebx
    jmp put_queue

num_table   db '789 456 1230.'
cur_table   db 'HA5 DGC YB623'

func:
    push eax 
    push ecx 
    push edx 
    call _show_stat     ; kernel/sched.c
    pop edx
    pop ecx
    pop eax
    sub al, 0x3b        ; func index
    jb end_func
    cmp al, 9
    jbe ok_func
    sub al, 0x12
    cmp al, 10
    jb end_func
    cmp al, 11
    jb end_func
ok_func:
    cmp ecx, 4      ; check for enough space
    jl end_func
    mov dword eax, [func_table+eax*4]
    xor ebx, ebx
    jmp put_queue
end_func:
    ret

func_table:
    dd 0x415b5b1b, 0x425b5b1b, 0x435b5b1b, 0x445b5b1b
    dd 0x455b5b1b, 0x465b5b1b, 0x475b5b1b, 0x485b5b1b
    dd 0x495b5b1b, 0x4a5b5b1b, 0x4b5b5b1b, 0x4c5b5b1b

key_map:
    db 0, 17, '1234567890-='
    db 127, 9, 'qwertyuiop[]'
    db 13, 0, 'asdfghjkl;', `\'`
    db '`', 0, `\\`, 'zxcvbnm,./'
    db 0, '*', 0, 32
    times 16 db 0
    db '-', 0, 0, 0, '+'
    db 0, 0, 0, 0, 0, 0, 0, '<'
    times 10 db 0

shift_map:
    db 0, 17, '!@#$%^&*()_+'
    db 127, 9, 'QWERTYUIOP{}'
    db 13, 0, 'ASDFGHJKL:', `\"`
    db '~', 0, '|ZXCVBNM<>?'
    db 0, '*', 0, 32
    times 16 db 0
    db '-', 0, 0, 0, '+'
    db 0, 0, 0, 0, 0, 0, 0, '>'
    times 10 db 0

alt_map:
    db 0, 0, `\0@\0$\0\0{[]}\\\0`
    times 13 db 0
    db '~', 13, 0
    times 56 db 0
    db '|'
    times 10 db 0

normal:
    lea ebx, alt_map
    test byte [mode], 0x20
    jne ok_normal
    lea ebx, shift_map
    test byte [mode], 0x03
    jne ok_normal
    lea ebx, key_map
ok_normal:
    mov byte al, [ebx+eax]
    or al, al
    je none
    test byte [mode], 0x4c
    je no_ctrl_caps
    cmp al, 'a'
    jb no_ctrl_caps
    cmp al, '}'
    ja no_ctrl_caps
    sub al, 0x20    ; lower --> upper
no_ctrl_caps:
    test byte [mode], 0x0c
    je no_ctrl
    cmp al, '@'
    jb no_ctrl
    cmp al, '`'
    jae no_ctrl
    sub al, 0x40
no_ctrl:
    test byte [mode], 0x10
    je no_alt
    or al, 0x80
no_alt:
    and eax, 0x000000ff
    xor ebx, ebx
    call put_queue
none:
    ret

minus:
    cmp byte [e0], 1
    jne normal
    mov eax, '/'
    xor ebx, ebx
    jmp put_queue

key_wait:
    push eax
wait_loop:
    in al, 0x64
    test al, 0x02
    jne wait_loop
    pop eax
    ret

reboot:
    call key_wait
    mov word [0x472], 0x1234
    mov al, 0xfc
    out 0x64, al
die:
    jmp die

key_table:
    dd none, normal, normal, normal
    dd normal, normal, normal, normal
    dd normal, normal, normal, normal
    dd normal, normal, normal, normal
    dd normal, normal, normal, normal
    dd normal, normal, normal, normal
    dd normal, normal, normal, normal
    dd normal, ctrl, normal, normal
    dd normal, normal, normal, normal
    dd normal, normal, normal, normal
    dd normal, normal, lshift, normal
    dd normal, normal, normal, normal
    dd normal, normal, normal, normal
    dd normal, minus, rshift, normal
    dd alt, normal, caps, func
    dd func, func, func, func
    dd func, func, func, func
    dd func, num, scroll, cursor
    dd cursor, cursor, normal, cursor
    dd cursor, cursor, normal, cursor
    dd cursor, cursor, cursor, cursor
    dd none, none, normal, func
    dd func, none, none, none
    dd none, none, none, none
    dd none, none, none, none
    dd none, none, none, none
    dd none, none, none, none
    dd none, none, none, none
    dd none, none, none, none
    dd none, none, none, none
    dd none, none, none, none
    dd none, none, none, none
    dd none, none, none, none
    dd none, none, none, none
    dd none, none, none, none
    dd none, none, none, none
    dd none, none, none, none
    dd none, none, none, none
    dd none, none, none, none
    dd none, unctrl, none, none
    dd none, none, none, none
    dd none, none, none, none
    dd none, none, unlshift, none
    dd none, none, none, none
    dd none, none, none, none
    dd none, none, unrshift, none
    dd unalt, none, uncaps, none
    dd none, none, none, none
    dd none, none, none, none
    dd none, none, none, none
    dd none, none, none, none
    dd none, none, none, none
    dd none, none, none, none
    dd none, none, none, none
    dd none, none, none, none
    dd none, none, none, none
    dd none, none, none, none
    dd none, none, none, none
    dd none, none, none, none
    dd none, none, none, none
    dd none, none, none, none
    dd none, none, none, none
    dd none, none, none, none
    dd none, none, none, none
