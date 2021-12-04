; Mirix 1.0/boot/setup.asm
; (C) 2021 Miris Lee

INITSEG	equ 0x9000
SETUPSEG	equ 0x9020
SYSSEG	equ 0x1000

jmp start

start:
	mov ax, INITSEG
	mov es, ax 
	mov ah, 0x03		; read cursor pos
	xor bh, bh 
	int 0x10
	mov word [es:0x0000], dx	; store it in known place
	
	mov ah, 0x88		; get extended memory size 
	int 0x15
	mov word [es:0x0002], ax 
	
	mov ah, 0x0f		; get video card mode
	int 0x10
	mov word [es:0x0004], bx	; display page 
	mov word [es:0x0006], ax	; video mode and window width
	
	mov ah, 0x12		; check for EGA/VGA
	mov bl, 0x10
	int 0x10
	mov word [es:0x0008], ax 
	mov word [es:0x000a], bx	; display memory and display state
	mov word [es:0x000c], cx	; video card characteristic parameter
	
	mov ax, 0x0000		; get hd0 data 
	mov ds, ax 
	lds si, [4*0x41]	; int vector 0x41 (hd0 data table)
	mov ax, INITSEG
	mov es, ax 
	mov di, 0x0080		; destination = 0x9000:0x0080
	mov cx, 0x10
	rep 
	movsb 
	
	mov ax, 0x0000		; get hd1 data 
	mov ds, ax 
	lds si, [4*0x46]	; int vector 0x46 (hd1 data table)
	mov ax, INITSEG
	mov es, ax 
	mov di, 0x0090		; destination = 0x9000:0x0090
	mov cx, 0x10
	rep 
	movsb 
	
	mov ax, 0x1500		; check if there is hd1
	mov dl, 0x81
	int 0x13
	jc no_hd1
	cmp ah, 3			; is it hard drive
	je is_hd1
no_hd1:
	mov ax, INITSEG	; clear hd1 data
	mov es, ax 
	mov di, 0x0090
	mov cx, 0x10
	mov ax, 0x00
	rep 
	stosb
is_hd1:

; *** move to protected mode ***
	cli					; no interrupt allowed

	mov ax, 0x0000		; move the system
	cld					; clear direction, moves forward
do_move:
	mov es, ax			; destination base
	add ax, 0x1000
	cmp ax, 0x9000		; have we finished moving
	jz end_move
	mov ds, ax			; source base
	xor di, di 
	xor si, si 
	mov cx, 0x8000		; 64kB
	rep 
	movsw 
	jmp do_move
	
end_move:				; load segment descriptors
	mov ax, SETUPSEG
	mov ds, ax 
	lidt idt_48
	lgdt gdt_48
	
	; enable A20
	call empty_8042
	mov al, 0xd1		; command write
	out 0x64, al		; P2 port of 8042
	call empty_8042
	mov al, 0xdf		; A20 on
	out 0x60, al 
	call empty_8042
	
	; reprogram the interrupts
	mov al, 0x11		; ICW1
	out 0x20, al		; 8259A-1
	dw 0x00eb, 0x00eb		; jmp $+2, jmp $+2 (delay)
	out 0xa0, al		; 8259A-2
	dw 0x00eb, 0x00eb
	mov al, 0x20		; ICW2-1
	out 0x21, al 
	dw 0x00eb, 0x00eb
	mov al, 0x28		; ICW2-2
	out 0xa1, al 
	dw 0x00eb, 0x00eb
	mov al, 0x04		; ICW3-1
	out 0x21, al 
	dw 0x00eb, 0x00eb
	mov al, 0x02		; ICW3-2
	out 0xa1, al 
	dw 0x00eb, 0x00eb
	mov al, 0x01		; ICW4
	out 0x21, al 
	dw 0x00eb, 0x00eb
	out 0xa1, al 
	dw 0x00eb, 0x00eb
	mov al, 0xff		; OCW1, ignore all hardware interrupts
	out 0x21, al 
	dw 0x00eb, 0x00eb
	out 0xa1, al 
	dw 0x00eb, 0x00eb
	
	; load machine status word 
	mov ax, 0x0001		; protected mode (PE) bit 
	lmsw ax 
	jmp 0x0008:0x0000		; jump to the beginning of segment #1 (cs)
	
empty_8042:
	dw 0x00eb, 0x00eb
	in al, 0x64			; 8042 status port
	test al, 2			; is input buffer full
	jnz empty_8042		; loop 
	ret 
	
gdt:
	; #0 segment (null)
	dw 0, 0, 0, 0
	; #1 segment (code)
	dw 0x07ff			; limit = 8MB (0x0800 * 4kB = 8MB)
	dw 0x0000			; base = 0x0000
	dw 0x9a00			; read/exec
	dw 0x00c0			; granularity = 4kB, i386
	; #2 segment (data)
	dw 0x07ff			; limit = 8MB (0x0800 * 4kB = 8MB)
	dw 0x0000			; base = 0x0000
	dw 0x9200			; read/write
	dw 0x00c0			; granularity = 4kB, i386
	
idt_48:
	dw 0					; limit = 0
	dw 0x0000, 0x0000			; base = 0x00000000
	
gdt_48:
	dw 2048				; limit = 2048 (256 entries)
	dw 0x0200+gdt, 0x0009	; base = 0x00090200 + offset of gdt in this file
	
times 512*4-($-$$) db 0