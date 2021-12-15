; Mirix 1.0/mm/page.asm
; (C) 2021 Miris Lee

global _page_fault

_page_fault:
	xchg dword [esp], eax	; get the error code 
	push ecx 
	push edx 
	push ds 
	push es 
	push fs 
	mov edx, 0x10				; data segment 
	mov ds, dx 
	mov es, dx 
	mov fs, dx 
	mov edx, cr2				; get the linear address of page fault
	push edx 
	push eax 
	test eax, 1					; flag P 
	jne wp_page
	call _do_no_page			; mm/memory.c 
	jmp ireturn
wp_page:
	call _do_wp_page			; mm/memory.c 
ireturn:
	add esp, 8					; discard 2 arguments
	pop fs 
	pop es 
	pop ds 
	pop edx 
	pop ecx 
	pop eax 
	iret