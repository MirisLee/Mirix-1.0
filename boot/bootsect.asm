; Mirix 1.0/boot/bootsect.asm
; (C) 2021 Miris Lee

SYSSIZE	equ	0x3000
SETUPLEN	equ	4
BOOTSEG	equ	0x07c0
INITSEG	equ	0x9000
SETUPSEG	equ 	0x9020
SYSSEG	equ	0x1000
ENDSYS	equ	(SYSSEG+SYSSIZE)

ROOTDEV	equ	0x306		; the 1st partition of the 2nd drive

jmp start

start:
	mov ax, BOOTSEG	; set ds to 0x07c0
	mov ds, ax 
	mov ax, INITSEG	; set es to 0x9000
	mov es, ax 
	mov cx, 256			; moving count = 256 (words)
	xor si, si			; source address ds:si = 0x07c0:0x0000
	xor di, di			; destination address es:di = 0x9000:0x0000
	rep 
	movsw 
	
	jmp INITSEG:go 
	
go:
	mov ax, cs			; set ds, es and ss to 0x9000
	mov ds, ax 
	mov es, ax 
	mov ss, ax 
	mov sp, 0xff00		; 0xff00 >> 512
	
load_setup:
	mov dx, 0x0000		; drive 0, head 0
	mov cx, 0x0002		; sector 2, track 0
	mov bx, 0x0200		; address = 0x0200 (in INITSEG)
	mov ax, 0x0200+SETUPLEN
	int 0x13				; read
	jnc ok_load_setup
	mov dx, 0x0000		; reset the diskette
	mov ax, 0x0000
	int 0x13
	jmp load_setup
	
ok_load_setup:
	; get drive information
	mov dl, 0x00
	mov ax, 0x0800
	int 0x13
	mov ch, 0x00
	mov word [sectors], cx		; store the number of sectors per track
	mov ax, INITSEG	; reset es to 0x9000
	mov es, ax 

	; print inane message
	mov ah, 0x03		; read cursor pos
	xor bh, bh 
	int 0x10
	mov cx, 24			; totally 24 characters
	mov bx, 0x0007		; page 0, attr 7 (normal)
	mov bp, msg1
	mov ax, 0x1301		; write string and move cursor
	int 0x10

	; load the system
	mov ax, SYSSEG
	mov es, ax 
	call read_it
	call kill_motor
	
	jmp SETUPSEG:0
	
sread	dw 1+SETUPLEN	; sectors read of current track
head	dw 0				; current head
track	dw 0				; current track

read_it:
	mov ax, es			; es must be at 64kB boundary
	test ax, 0x0fff
die:
	jne die
	xor bx, bx			; starting address within segment 

rp_read:
	mov ax, es 
	cmp ax, ENDSEG		; have we loaded all yet
	jb ok1_read
	ret 
ok1_read:
	mov word ax, [sectors]
	sub word ax, [sread]
	mov cx, ax			; sectors unread of current track
	shl cx, 9			; the number bytes per sector is 512 (2^9)
	add cx, bx 
	jnc ok2_read
	je ok2_read
	xor ax, ax 
	sub ax, bx			; maximum bytes for loading
	shr ax, 9
ok2_read:
	call read_track
	mov cx, ax 
	add word ax, [sread]
	cmp word ax, [sectors]		; there are still unread sectors
	jne ok3_read
	mov ax, 1
	sub word ax, [head]			; current head
	jne ok4_read
	inc track
ok4_read:
	mov word [head], ax		; store current head
	xor ax, ax 
ok3_read:
	mov word [sread], ax 
	shl cz, 9
	add bx, cx			; change starting address within segment 
	jnc rp_read
	
	mov ax, es 
	add ax, 0x1000		; change es to the next 64kB
	mov es, ax 
	xor bx, bx 
	jmp rp_read
	
read_track:
	push ax 
	push bx 
	push cx 
	push dx 
	mov word dx, [track]		; current track
	mov word cx, [sread]
	inc cx 				; cl = starting sector
	mov ch, dl			; ch = current track
	mov word dx, [head]		; current head
	mov dh, dl 
	mov dl, 0			; dl = drive no.
	and dx, 0x0100
	mov ah, 2
	int 0x13
	jc bad_rt			; error
	pop dx 
	pop cx 
	pop bx 
	pop ax 
	ret 
bad_rt:
	mov ax, 0
	mov dx, 0
	int 0x13
	pop dx 
	pop cx 
	pop bx 
	pop ax 
	jmp read_track
	
kill_motor:
	push dx 
	mov dx, 0x03f2		; drive port, write only
	mov al, 0
	out dx, al 
	pop dx 
	ret 
	
sectors	dw 0

msg1:
	db 13, 10			; cursor return, line feed
	db 'Loading system ...'
	db 13, 10, 13, 10
	
times 512-4-($-$$) db 0

root_dev		dw ROOTDEV
boot_flag	dw 0xaa55