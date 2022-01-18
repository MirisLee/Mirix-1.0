; Mirix 1.0/kernel/chr_dev/rs_io.asm
; (C) 2022 Miris Lee

global _rs1_int, _rs2_int
extern _table_list, _do_tty_int

buf_size    equ 1024
rs_addr     equ 0
head        equ 4
tail        equ 8
proc_list   equ 12
buf         equ 16
startup     equ 256

align 2
_rs1_int:
    push _table_list+8
    jmp rs_int

align 2
_rs2_int:
    push _table_list+16
rs_int:
    push edx
    push ecx
    push ebx
    push eax
    push es
    push ds
    push 0x10
    pop ds
    push 0x10
    pop es
    mov edx, dword [esp+24]
    mov edx, dword [edx]        ; read queue address
    mov edx, dword [rs_addr+edx]
    add edx, 2
loop_int:
    xor eax, eax
    in al, dx
    test al, 1
    jne end_int
    cmp al, 6
    ja end_int
    mov ecx, dword [esp+24]
    push edx
    sub edx, 2
    call jmp_table+eax*2    ; eax has multiplied 2
    pop edx
    jmp loop_int
end_int:
    mov al, 0x20
    out 0x20, al        ; EOI
    pop ds
    pop es
    pop eax
    pop ebx
    pop ecx
    pop edx
    add esp, 4          ; jump over _table_list entry
    iret

jmp_table:
    dd modem, line, read_char, write_char

align 2
modem:
    add edx, 6
    in al, dx       ; read modem status register
    ret

align 2
line:
    add edx, 5
    in al, dx       ; read line status register
    ret

align 2
read_char:
    in al, dx
    mov edx, ecx
    sub edx, _table_list
    shr edx, 3      ; serial nr.
    mov ecx, dword [ecx]    ; read queue
    mov ebx, dword [ecx+head]
    mov byte [ecx+ebx+buf], al
    inc ebx
    and ebx, buf_size-1
    cmp ebx, dword [ecx+tail]
    je read_proc
    mov dword [ecx+head], ebx
read_proc:
    push edx
    call _do_tty_int
    add esp, 4
    ret

align 2
write_char:
    mov ecx, dword [ecx+4]  ; write queue
    mov ebx, dword [ecx+head]
    sub ebx, dword [ecx+tail]
    and ebx, buf_size-1
    je write_buf_empty
    cmp ebx, startup
    ja write_proc
    mov ebx, dword [ecx+proc_list]  ; wake up process
    test ebx, ebx
    je write_proc
    mov dword [ebx], 0
write_proc:
    mov ebx, dword [ecx+tail]
    mov al, byte [ecx+ebx+buf]
    out dx, al
    inc ebx
    and ebx, buf_size-1
    mov dword [ecx+tail], ebx
    cmp ebx, dword [ecx+head]
    je write_buf_empty
    ret

align 2
write_buf_empty:
    mov ebx, dword [ecx+proc_list]
    test ebx, ebx
    je no_proc
    mov dword [ebx], 0
no_proc:
    inc edx
    in al, dx
    db 0xeb, 0xeb
    and al, 0x0d    ; disable transmit int
    out dx, al
    ret