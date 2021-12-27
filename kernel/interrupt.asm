; Mirix 1.0/kernel/interrupt.asm
; (C) 2021 Miris Lee
global _divide_error, _debug, _nmi, _int3
global _overflow, _bounds, _invalid_op, _double_fault
global _coprocessor_segment_overrun, _invalid_TSS, _segment_not_present, _stack_segment
global _general_protection, _reserved

no_error_code:
	; push _do_...
	xchg dword [esp], eax	; address of C function
	push ebx 
	push ecx 
	push edx 
	push edi 
	push esi 
	push ebp 
	push ds 
	push es 
	push fs 
	push 0	; error code 
	lea edx, [esp+44]
	push edx 
	mov edx, 0x10	; code segment
	mov ds, dx 
	mov es, dx 
	mov fs, dx 
	call *eax	; call C function
	add esp, 8
	pop fs 
	pop es 
	pop ds 
	pop ebp 
	pop esi 
	pop edi 
	pop edx 
	pop ecx 
	pop ebx 
	pop eax 
	iret 
	
error_code:
	; push _do_...
	xchg dword [esp+4], eax		; error code
	xchg dword [esp], ebx		; address of C function
	push ecx 
	push edx 
	push edi 
	push esi 
	push ebp 
	push ds 
	push es 
	push fs 
	push eax		; error code 
	lea eax, [esp+44]
	push eax 
	mov eax, 0x10	; code segment 
	mov ds, ax 
	mov es, ax 
	mov fs, ax 
	call *ebx	; call C function
	add esp, 8
	pop fs 
	pop es 
	pop ds 
	pop ebp 
	pop esi 
	pop edi 
	pop edx 
	pop ecx 
	pop ebx 
	pop eax 
	iret 
	
_divide_error:
	push _do_divide_error
	jmp no_error_code
	
_debug:
	push _do_int3
	jmp no_error_code
	
_nmi:
	push _do_nmi
	jmp no_error_code

_int3:
	push _do_int3
	jmp no_error_code
	
_overflow:
	push _do_overflow
	jmp no_error_code

_bounds:
	push _do_bounds
	jmp no_error_code
	
_invalid_op:
	push _do_invalid_op
	jmp no_error_code
	
; _device_not_available -- kernel/syscall.asm

_double_fault:
	push _do_double_fault
	jmp error_code

_coprocessor_segment_overrun:
	push _do_coprocessor_segment_overrun
	jmp no_error_code
	
_invalid_TSS:
	push _do_invalid_TSS
	jmp error_code
	
_segment_not_present:
	push _do_segment_not_present
	jmp error_code
	
_stack_segment:
	push _do_stack_segment
	jmp error_code
	
_general_protection:
	push _do_general_protection
	jmp error_code
	
; _page_fault -- mm/page.asm

_reserved:
	push _do_reserved
	jmp no_error_code
	
; _coprocessor_error -- kernel/syscall.asm
; _time_interrupt -- kernel/syscall.asm
; _system_call -- kernel/syscall.asm