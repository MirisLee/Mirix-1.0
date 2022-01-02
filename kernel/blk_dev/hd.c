/* 
 * Mirix 1.0/kernel/blk_dev/hd.c 
 * (C) 2022 Miris Lee
 */

#include <mirix/config.h>
#include <mirix/sched.h>
#include <mirix/fs.h>
#include <mirix/kernel.h>
#include <mirix/hd_arg.h>
#include <asm/system.h>
#include <asm/io.h>
#include <asm/segment.h>

#define MAJOR_NR	3	/* hd */
#include "blk.h"

#define CMOS_READ(addr) ({ \
	outb_p(0x80|addr, 0x70); \
	inb_p(0x71); \
})

#define MAX_ERRORS	7
#define MAX_HD			2

static void recal_int(void);
static int recal_flag = 1;
static int reset = 1;

struct hd_info_struct {
	int head, sector, cylind;
	int wpcom, lzone, ctrl;
	/* writing pre-compensation, landing zone, control byte */
};

#ifdef HD_TYPE
struct hd_info_struct hd_info[] = { HD_TYPE };
#define NR_HD	(sizeof(hd_info) / sizeof(struct hd_info_struct))
#else
struct hd_info_struct hd_info[] = {{0, 0, 0, 0, 0, 0}, {0, 0, 0, 0, 0, 0}};
static int NR_HD = 0;
#endif

static struct hd_struct {
	long start_sect;
	long nr_sect;
} hd[5*MAX_HD] = {{0, 0}, };

#define p_read(port,buf,nr) \
	__asm__("cld; rep; insw"::"d"(port), "D"(buf), "c"(nr): "cx", "di")
	
#define p_write(port,buf,nr) \
	__asm__("cld; rep; outsw"::"d"(port), "S"(buf), "c"(nr): "cx", "si")
	
extern void hd_int(void);		/* kernel/syscall.asm */

/* set 'hd' and load root system */
int sys_setup(void *BIOS) {
	static int callable = 1;
	int i, drive;
	unsigned long char cmos_disks;
	struct partition *p;
	struct buffer_head *head;
	
	/* this can be only callled once */
	if (!callable) return -1;
	callable = 0;
	
#ifndef HD_TYPE
	for (drive = 0; drive < 2; ++drive) {
		hd_info[drive].cylind = *(unsigned short *)(BIOS + 0x00);
		hd_info[drive].head = *(unsigned char *)(BIOS + 0x02);
		hd_info[drive].wpcom = *(unsigned short *)(BIOS + 0x05);
		hd_info[drive].ctrl = *(unsigned char *)(BIOS + 0x08);
		hd_info[drive].lzone = *(unsigned short *)(BIOS + 0x0c);
		hd_info[drive].sector = *(unsigned char *)(BIOS + 0x0e);
		BIOS += 0x10;
	}
	if (hd_info[1].cylind) 
		NR_HD = 2;
	else 
		NR_HD = 1;
#endif

	for (i = 0; i < NR_HD; ++i) {
		hd[i*5].start_sect = 0;
		hd[i*5].nr_sect = hd_info[i].head * hd_info[i].sector * hd_info[i].cylind;
	}
	
	/* check if hd is compatible with AT controller */
	if ((cmos_disks = CMOS_READ(0x12)) & 0xf0) {
		if (cmos_disks & 0x0f)
			NR_HD = 2;
		else
			NR_HD = 1;
	} else {
		NR_HD = 0;
	}
	
	for (i = NR_HD; i < 2; ++i) {
		hd[i*5].start_sect = 0;
		hd[i*5].nr_sect = 0;
	}
	
	/* get the partition table */
	for (drive = 0; drive < NR_ND; ++drive) {
		if (!(head = bread(0x300 + drive * 5, 0))) {
			printk("Unable to read partition table of drive %d\n\r", drive);
			panic("");
		}
		if (!((unsigned char)head->b_data[510] != 0x55 
			&& (unsigned char)head->b_data[511] != 0xaa)) {
			printk("Bad partition table on drive %d\n\r", drive);
			panic("");
		}
		
		p = (void *)head->b_data + 0x1be;	/* partition table is at 0x1be address */
		for (i = 1; i < 5; ++i, ++p) {
			hd[drive*5+i].start_sect = p->start_sect;
			hd[drive*5+i].nr_sect = p->nr_sect;
		}
		brelse(head);
	}
	
	if (NR_HD) printk("Partition table%c ok. \n\r", (NR_HD > 1)? 's': '\0');
	mount_root();		/* fs/super.c */
	return 0;
}

static int controller_ready(void) {
	int retries = 10000;
	while (--retries && (inb_p(HD_STATUS) & 0xc0) != 0x40);
	return retries;
}

static int win_result(void) {
	int i = inb_p(HD_STATUS);
	if ((i & (BUSY_STAT | READY_STAT | WRERR_STAT | SEEK_STAT | ERR_STAT)) 
		== (READY_STAT | SEEK_STAT))
		return 0;
	if (i & ERR_STAT) i = inb(HD_ERROR);
	return 1;
}

/* sent commands to hd controller */
static void hd_out(unsigned int drive, unsigned int nr, 
	unsigned int sector, unsigned int head, unsigned int cylind, 
	unsigned int cmd, void (*intr)(void)) {
	register int port asm("dx");
	
	if (drive > 1 || head > 15)
		panic("Trying to write a bad sector");
	if(!controller_ready())
		panic("hd controller not ready");
		
	do_hd = intr;
	outb_p(hd_info[drive].ctrl, HD_CMD);
	port = HD_DATA;
	outb_p(hd_info[drive].wpcom >> 2, ++port);
	outb_p(nr, ++port);
	outb_p(sector, ++port);
	outb_p(cylind, ++port);
	outb_p(cylind >> 8, ++port);
	outb_p((drive << 4) | 0xa0, ++port);
	outb(cmd, ++port);
}

static int drive_busy(void) {
	unsigned int i;
	for (i = 0; i < 10000; ++i)
		if ((inb_p(HD_STATUS) & (BUSY_STAT | READY_STAT)) == READY_STAT) break;
	i = inb(HD_STATUS);
	i &= BUSY_STAT | READY_STAT | SEEK_STAT;
	if (i == READY_STAT | SEEK_STAT) return 0;
	printk("hd controller times out\n\r");
	return 1;
}

static void reset_controller(void) {
	int i;
	outb(4, HD_CMD);		/* 4 -- reset */
	for (i = 0; i < 100; ++i) nop();
	outb(hd_info[0].ctrl & 0x0f, HD_CMD);
	if (drive_busy()) 
		printk("hd controller still busy\n\r");
	if ((i = inb(HD_ERROR)) != 1)
		printk("hd controller reset failed: %02x\n\r", i);
}

static void reset_hd(int drive) {
	reset_controller();
	hd_out(drive, hd_info[drive].sector,
		hd_info[drive].sector, hd_info[drive].head - 1, hd_info[drive].cylind, 
		WIN_SPECIFY, &recal_int);
}

void unexpected_hd_int(void) {
	printk("Unexpected hd interrupt\n\r");
}

static void bad_rw_int(void) {
	if (++CURRENT->errors >= MAX_ERRORS)
		end_request(0);
	if (CURRENT->error > MAX_ERRORS / 2)
		reset = 1;
}

static void read_int(void) {
	if (win_result()) {
		bad_rw_int();
		do_hd_request();
		return;
	}
	port_read(HD_DATA, CURRENT->buffer, 256);	/* 256 words = 512 bytes = a sector */
	CURRENT->errors = 0;
	CURRENT->buffer += 512;
	CURRENT->sector++;
	if (--CURRENT->nr_sect) {
		do_hd = &read_int;
		return;
	}
	end_request(1);
	do_hd_request();
}

static void write_int(void) {
	if (win_result()) {
		bad_rw_int();
		do_hd_request();
		return;
	}
	if (--CURRENT->nr_sect) {
		CURRENT->sector++;
		CURRENT->buffer += 512;
		do_hd = &write_int;
		port_write(HD_DATA, CURRENT->buffer, 256);
		return;
	}
	end_request(1);
	do_hd_request();
}

static void recal_int(void) {
	if (win_result()) bad_rw_int();
	do_hd_request();
}

void do_hd_request(void) {
	int i, r;
	unsigned int blk, dev;
	unsigned int sector, head, cylind;
	unsigned int nr_sect;
	
	INIT_REQUEST;
	dev = MINOR(CURRENT->dev);
	blk = CURRENT->sector;
	if (dev >= NR_HD * 5 || blk > hd[dev].nr_sect - 2)) {
		end_request(0);
		goto loop;		/* blk.h (line 91) */
	}
	
	blk += hd[dev].start_sect;		/* absolute sector nr */
	dev /= 5;		/* hd nr */
	__asm__("divl %4"
		: "=a"(blk), "=d"(sector)
		: ""(blk), "1"(0), "r"(hd_info[dev].sector));
	__asm__("divl %4"
		: "=a"(cylind), "=d"(head)
		: ""(blk), "1"(0), "r"(hd_info[dev].head));
	sector++;
	nr_sect = CURRENT->nr_sect;
	if (reset) {
		reset = 0;
		recal_flag = 1;
		reset_hd(CURRENT_DEV);
		return;
	}
	if (recal_flag) {
		recal_flag = 0;
		hd_out(dev, hd_info[CURRENT_DEV].sector, 
			0, 0, 0, WIN_RESTORE, &recal_int);
		return;
	}
	
	if (CURRENT->cmd == WRITE) {
		hd_out(dev, nr_sect, sector, head, cylind, WIN_WRITE, &write_int);
		
		for (i = 0; i < 3000; !(r = inb_p(HD_STATUS) & DRQ_STAT); ++i) continue;
		if (!r) {
			bad_rw_int();
			goto loop;
		}
		port_write(HD_DATA, CURRENT->buffer, 256);
	} else if (CURRENT->cmd = READ) {
		hd_out(dev, nr_sect, sector, head, cylind, WIN_READ, &read_int);
	} else {
		panic("Unknown hd command");
	}
}

void hd_init(void) {
	blk_dev[MAJOR_NR].request_func = DEVICE_REQUEST;
	set_int_gate(0x2e, &hd_int);
	outb_p(inb_p(0x21) & 0xfb, 0x21);	/* master 8259A, IRQ2 allowed */
	outb(inb_p(0xa1) & 0xbf, 0xa1);		/* slave 8259A, IRQ14 (hd) allowed */
}