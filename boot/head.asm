; Mirix 1.0/boot/head.asm
; (C) 2021 Miris Lee

global _idt, _gdt, _pg_dir, _tmp_floppy_area

_pg_dir:

; code here will be covered by the page directory
startup_32:
	mov eax, 0x0010		; data segment selector
	mov ds, ax 
	mov es, ax 
	mov fs, ax 
	mov gs, ax 
	lss esp, _stack_start	; set system stack, kernel/sched.c 
	call setup_idt
	call setup_gdt
	mov eax, 0x0010		; reload segment registers
	mov ds, ax 
	mov es, ax 
	mov fs, ax 
	mov gs, ax 
	lss esp, _stack_start
	
	; check A20
	xor eax, eax 
loop1:
	inc eax 
	mov dword [0x000000], eax 
	cmp dword eax, [0x100000]	; 0x000000 and 0x100000 will be the same if A20 hasn' t been set 
	je loop1					; loop forever
	
	jmp after_page_tables
	
setup_idt:
	lea edx, ignore_int		; all interrupt gates will point to 'ignore_int'
	mov eax, 0x00080000		; set the segment selector
	mov ax, dx				; set the low 16 bits of the offset 
	mov dx, 0x8e00			; interrupt gate
	lea edi, _idt
	mov ecx, 256
rep_set_idt:
	mov dword [edi+0x00], eax 	; write in te interrupt gate
	mov dword [edi+0x04], edx 
	add edi, 0x08			; move to the next
	dec ecx 
	jne rep_set_idt
	lidt idt_desc			; load IDTR
	ret 
	
setup_gdt:
	lgdt gdt_desc			; load GDTR
	ret 
	
org 0x1000
pg0: 

org 0x2000
pg1: 

org 0x3000
pg2:

org 0x4000
pg3: 

org 0x5000
_tmp_floppy_area:
	times 1024 db 0
	
after_page_tables:
	; parameters to main
	push 0				; envp
	push 0				; argv
	push 0				; argc
	push ret_addr		; return address of main
	push _main
	jmp setup_paging
ret_addr:
	jmp ret_addr		; main should never return here
	
int_msg:
	db 'Unknown interrupt', 13, 10
	
align 2
ignore_int:
	push eax 
	push ecx 
	push edx 
	push ds 
	push es 
	push fs 
	mov eax, 0x0010	; set segment 
	mov ds, ax 
	mov es, ax 
	mov fs, ax 
	push int_msg
	call _printk		; kernel/printk.c
	pop eax 
	pop fs 
	pop es 
	pop ds 
	pop edx 
	pop ecx 
	pop eax 
	iret					; interrupt return
	
align 2
setup_pagging:
	mov ecx, 5*1024	; pg_dir + 4 page tables
	xor eax, eax 
	xor edi, edi 
	cld 
	rep 
	stosd 
	; set page directory entries
	mov dword [_pg_dir+0x00], pg0+7
	mov dword [_pg_dir+0x04], pg1+7
	mov dword [_pg_dir+0x08], pg2+7
	mov dword [_pg_dir+0x0c], pg3+7
	; fill in all page table entries
	mov edi, pg3+4092		; the last entry of the last table
	mov eax, 0xfff007		; phsical page address + attr_flag
	std 
filling:
	stosd 
	sub eax, 0x1000
	jge filling
	xor eax, eax			; address of pg_dir
	mov eax, cr3			; page directory start 
	mov eax, cr0 
	or eax, 0x80000000	; PG flag
	mov cr0, eax 
	ret						; pop the address of main, and call /init/main.c 
	
align 2
dw 0
idt_desc:
	dw 256*8-1
	dd _idt

align 2
dw 0
gdt_desc:
	dw 256*8-1
	dd _gdt
	
align 3
_idt:
	times 256 dq 0
	
_gdt:
	dq 0x0000000000000000	; #0 (null)
	dq 0x00c09a0000000fff	; #1 (cs)
	dq 0x00c0920000000fff	; #2 (ds)
	dq 0x0000000000000000	; #3 (sys)
	times 252 dq 0			; LDT and TSS