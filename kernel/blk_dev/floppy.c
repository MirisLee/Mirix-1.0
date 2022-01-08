/* 
 * Mirix 1.0/kernel/blk_dev/floppy.c 
 * (C) 2022 Miris Lee
 */

#include <mirix/sched.h>
#include <mirix/fs.h>
#include <mirix/kernel.h>
#include <mirix/floppy_arg.h>
#include <asm/system.h>
#include <asm/io.h>
#include <asm/segment.h>

#define MAJOR_NR	2	/* floppy */
#include "blk.h"

static int recal_flag = 0;
static int reset = 0;
static int seek = 0;

extern unsigned char cur_DOR;	/* kernel/sched.c */

#define immountb_p(val,port) \
	__asm__( \
		"outb %0, %1	\n\t" \
		"jmp 1f			\n" \
		"1:\t" \
		"jmp 1f			\n" \
		"1:" \
		:: "a"((char)(val)), "i"(port))
		
#define TYPE(minor) ((minor) >> 2)		/* 2 -- 1.2Mb, 7 -- 1.44Mb */
#define DRIVE(minor) ((minor) & 3)		/* 0~3 -- A~D */

#define MAX_ERRORS	8
#define MAX_REPLIES	7
static unsigned char reply_buffer[MAX_REPLIES];
#define ST0 (reply_buffer[0])
#define ST1 (reply_buffer[1])
#define ST2 (reply_buffer[2])
#define ST3 (reply_buffer[3])

static struct floppy_struct {
	unsigned int size, sector, head, track;
	unsigned int strech;
	unsigned char gap, rate;
	unsigned char arg;	/* high 4 bits -- step rate, low 4 bits -- head uploading time */
} floppy_type[] = {
	{ 0, 0, 0, 0, 0, 0x00, 0x00, 0x00 },	/* null */
	{ 720, 9, 2, 40, 0, 0x2a, 0x02, 0xdf },	/* 360kB PC diskette */
	{ 2400, 15, 2, 80, 0, 0x1b, 0x00, 0xdf },	/* 1.2MB AT-diskette */
	{ 720, 9, 2, 40, 1, 0x2a, 0x02, 0xdf },	/* 360kB in 720kB drive */
	{ 1440, 9, 2, 80, 0, 0x2a, 0x02, 0xdf },	/* 3.5" 720kB diskette */
	{ 720, 9, 2, 40, 1, 0x23, 0x01, 0xdf },	/* 360kB in 1.2MB drive */
	{ 1440, 9, 2, 80, 0, 0x23, 0x01, 0xdf },	/* 720kB in 1.2MB drive */
	{ 2880, 18, 2, 80, 0, 0x1b, 0x00, 0xcf },
	/* 1.44MB diskette */
};

extern void floppy_int(void);		/* kernel/syscall.asm */
extern char tmp_floppy_area[1024];		/* boot/head.asm */

static int cur_arg = -1;
static int cur_rate = -1;
static struct floppy_struct *floppy = floppy_type;
static unsigned char cur_drive = 0;
static unsigned char sector = 0, head = 0, track = 0, seek_track = 0, cur_track = 255, cmd = 0;

unsigned char sel = 0;
struct task_struct *wait = NULL;

void floppy_deselect(unsigned int nr) {
	if (nr != (cur_DOR & 3))
		printk("floppy_deselect: drive not selected\n\r");
	sel = 0;
	wake_up(&wait);
}

int floppy_change(unsigned int nr) {
loop:
	floppy_on(nr);		/* kernel/sched.c */
	while ((cur_DOR & 3) != nr && sel)
		int_sleep_on(&wait);		/* interruptible sleep */
	if ((cur_DOR & 3) != nr) goto loop;
	floppy_off(nr);
	if (inb(FLOPPY_DIR) & 0x80) return 1;
	return 0;
}

#define copy_buffer(src,dest) \
	__asm__(
		"cld; rep; movsl" \
		:: "c"(BLOCK_SIZE / 4), "S"((long)(src)), "D"((long)(dest)) \
		: "cx", "di", "si")
		
static void setup_DMA(void) {
	long addr = (long)CURRENT->buffer;
	
	cli();
	/* set DMA buffer */
	if (addr >= 0x100000) {
		addr = (long)tmp_floppy_area;
		if (command == FLOPPY_WRITE)
			copy_buffer(CURRENT->buffer, tmp_floppy_area);
	}
	/* mask DMA2 */
	immountb_p(4 | 2, 10);
	/* output command byte */
	__asm__(
		"outb %%al, $12	\n\t"
		"jmp 1f				\n"
		"1:\t"
		"jmp 1f				\n"
		"1:\t"
		"outb %%al, $11\n\t"
		"jmp 1f			\n"
		"1:\t"
		"jmp 1f			\n"
		"1:\t"
		:: "a"((char)((command == FLOPPY_READ)? DMA_READ: DMA_WRITE)));
	immountb_p(addr, 4);		/* DMA2 base/current address register */
	addr >>= 8;
	immountb_p(addr, 4);
	addr >>= 8;
	immountb_p(addr, 0x81);	/* page register */
	immountb_p(0xff, 5);		/* DMA2 base/current byte count */
	immountb_p(0x03, 5);
	immountb_p(0 | 2, 10);	/* activate DMA2 */
	sti();
}

/* output a byte to FDC */
static void output_byte(char byte) {
	int i;
	unsigned char status;
	
	if (reset) return;
	for (i = 0; i < 10000; ++i) {
		status = inb_p(FLOPPY_STATUS) & (READY_STAT | STAT_DIR);
		if (status = READY_STAT) {
			outb(byte, FLOPPY_DATA);
			return;
		}
	}
	reset = 1;
	printk("Unable to send byte to FDC\n\r");
}

static int result(void) {
	int i, j = 0, status;
	
	if (reset) return -1;
	for (i = 0; i < 10000; ++i) {
		status = inb_p(FD_STATUS) & (STAT_DIR | READY_STAT | BUSY_STAT);
		if (status == READY_STAT) return j;
		if (status == (STAT_DIR | READY_STAT | BUSY_STAT)) {
			if (j >= MAX_REPLIES) break;
			reply_buffer[j++] = inb_p(FLOPPY_DATA);
		}
	}
	reset = 1;
	printk("Geting status times out\n\r");
	return -1;
}

static void bad_floppy_int(void) {
	CURRENT->errors++;
	if (CURRENT->errors > MAX_ERRORS) {
		floppy_deselect(cur_drive);
		end_request(0);
	}
	if (CURRENT->errors > MAX_ERRORS / 2)
		reset = 1;
	else 
		recal_flag = 1;
}

static void rw_int(void) {
	if (result() != 7 || (ST0 & 0xf8) || (ST1 & 0xbf) || (ST2 & 0x73)) {
		if (ST1 & 0x02) {
			printk("Drive %d is write protected\n\r", cur_drive);
			floppy_deselect(cur_drive);
			end_request(0);
		} else {
			bad_floppy_int();
		}
		do_floppy_request();
		return;
	}
	
	if (cmd == FLOPPY_READ && (unsigned long)(CURRENT->buffer) >= 0x100000)
		copy_buffer(tmp_floppy_area, CURRENT->buffer);
	floppy_deselect(cur_drive);
	end_request(1);
	do_floppy_request();
}

inline void setup_floppy(void) {
	setup_DMA();
	do_floppy = rw_int;
	output_byte(cmd);
	output_byte(head << 2 | cur_drive);
	output_byte(track);
	output_byte(head);
	output_byte(sector);
	output_byte(2);		/* sector size: 512 */
	output_byte(floppy->sector);
	output_byte(floppy->gap);
	output_byte(0xff);
	
	if(reset) do_floppy_request();
}

static void seek_int(void) {
	output_byte(FLOPPY_SENSEI);
	if (result() != 2 || (ST0 & 0xf8) != 0x20 || ST1 != seek_track) {
		bad_floppy_int();
		do_floppy_request();
		return;
	}
	cur_track = ST1;
	setup_floppy();
}

static void transfer(void) {
	if (cur_arg != floppy->arg){
		cur_arg = floppy->arg;
		output_byte(FLOPPY_SPECIFY);
		output_byte(cur_arg);
		output_byte(6);		/* head load time = 6ms */
	}
	if (cur_rate != floppy->rate)
		outb_p(cur_rate = floppy->rate, FLOPPY_DCR);
	if (reset) {
		do_floppy_request();
		return;
	}
	if (!seek) {
		setup_floppy();
		return;
	}
	do_floppy = seek_int;
	if (seek_track) {
		output_byte(FLOPPY_SEEK);
		output_byte(head << 2 | cur_drive);
		output_byte(seek_track);
	} else {
		output_byte(FLOPPY_RECAL);
		output_byte(head << 2 | cur_drive);
	}
	if (reset) do_floppy_request();
}

static void recal_int(void) {
	output_byte(FLOPPY_SENSEI);
	if (result() != 2 || (ST0 & 0xe0) == 0x60)
		reset = 1;
	else
		recal_flag = 0;
	do_floppy_request();
}

void unexpected_floppy_int(void) {
	output_byte(FLOPPY_SENSEI);
	if (result() != 2 || (ST0 & 0xe0) == 0x60)
		reset = 1;
	else
		recal_flag = 1;
}

static void recal_floppy(void) {
	recal_flag = 0;
	cur_track = 0;
	do_floppy = recal_int;
	output_byte(FLOPPY_RECAL);
	output_byte(head << 2 | cur_drive);
	if (reset) do_floppy_request();
}

static void reset_int(void) {
	output_byte(FLOPPY_SENSEI);
	(void)result();
	output_byte(FLOPPY_SPECIFY);
	output_byte(cur_arg);
	output_byte(6);
	do_floppy_request();
}

static void reset_floppy(void) {
	int i;
	
	reset = 0;
	cur_arg = -1;
	cur_rate = -1;
	recal_flag = 1;
	printk("reset_floppy called\n\r");
	cli();
	do_floppy = reset_int;
	outb_p(cur_DOR & ~0x04, FLOPPY_DOR);
	for (i = 0; i < 100; ++i) __asm__("nop");
	outb(cur_DOR, FLOPPY_DOR);
	sti();
}

static void floppy_on_int(void) {
	sel = 1;
	if (cur_drive != (cur_DOR & 3)) {
		cur_DOR &= 0xfc;
		cur_DOR |= cur_drive;
		outb(cur_DOR, FLOPPY_DOR);
		add_timer(2, &transfer);
	} else {
		transfer();
	}
}

void do_floppy_request(void) {
	unsigned int blk;
	
	seek = 0;
	if (reset) {
		reset_floppy();
		return;
	}
	if (recal_flag) {
		recal_floppy();
		return;
	}
	
	INIT_REQUEST;
	floppy = floppy_type + (MINOR(CURRENT->dev) >> 2);
	if (cur_drive != CURRENT_DEV) seek = 1;
	cur_drive = CURRENT_DEV;
	blk = CURRENT->sector;
	if (blk > floppy->size - 2) {
		end_request(0);
		goto loop;
	}
	
	sector = blk % floppy->sector;
	blk /= floppy->sector;
	head = blk % floppy->head;
	track = blk / floppy->head;
	seek_track = track << floppy->stretch;
	if (seek_track != cur_track) seek = 1;
	sector++;
	
	if (CURRENT->cmd == READ)
		cmd = FLOPPY_READ;
	else if (CURRENT->cmd == WRITE)
		cmd = FLOPPY_WRITE;
	else 
		panic("do_floppy_request: unknown command");
		
	add_timer(ticks_to_floppy_on(cur_drive), &floppy_on_int);
}

void floppy_int(void) {
	blk_dev[MAJOR_NR].request_func = DEVICE_REQUEST;
	set_trap_gate(0x26, &floppy_int);
	outb(intb_p(0x21) & 0xbf, 0x21);	/* master 8259A, IRQ6(floppy) allowed */
}