/* 
 * Mirix 1.0/init/main.c 
 * (C) 2021 Miris Lee
 */

#define __LIBRARY__
#include <unistd.h>
#include <time.h>

/* 
 * we need to use inline fuction to protect 
 * the stack, and no function call is 
 * allowed. So, we have to create inline 
 * fork() and pause() to avoid system call
 */

/* _syscall0 & _syscall1 -> include/unistd.h */
static inline _syscall0(int, fork)
static inline _syscall0(int, pause)
static inline _syscall1(int, setup, void *, BIOS)
static inline _syscall0(int, sync)

#include <mirix/tty.h>
#include <mirix/sched.h>
#include <mirix/head.h>
#include <asm/system.h>
#include <asm/io.h>
#include <stddef.h>
#include <stdarg.h>
#include <fcntl.h>
#include <sys/types.h>
#include <mirix/fs.h>

static char printbuf[1024];

extern int vsprintf();		/* this means that vsprintf may get arguments with unknown type */
extern void init(void);
extern void blk_dev_init(void);
extern void chr_dev_init(void);
extern void hd_init(void);
extern void floppy_init(void);
extern void mem_init(long start, long end);
extern long kernel_mktime(struct tm *tm);
extern long startup_time;

/* data set by setup.asm */
#define EXT_MEM_K (*(unsigned short *)0x90002)
#define DRIVE_INFO (*(struct drive_info *)0x90080)
#define ORIG_ROOT_DEV (*(unsigned short *)0x901fc)

#define CMOS_READ(addr) ({ \
	outb_p(0x80|addr, 0x70); \
	inb_p(0x71); \
})

#define BCD_TO_BIN(num) \
	((num) = ((num) & 15) + ((num) >> 4) * 10)


static void time_init(void) {
	struct tm time;

	do {
		time.tm_sec = CMOS_READ(0);
		time.tm_min = CMOS_READ(2);
		time.tm_hour = CMOS_READ(4);
		time.tm_mday = CMOS_READ(7);
		time.tm_mon = CMOS_READ(8);
		time.tm_year = CMOS_READ(9);
	} while (time.tm_sec != CMOS_READ(0));
	BCD_TO_BIN(time.tm_sec);
	BCD_TO_BIN(time.tm_min);
	BCD_TO_BIN(time.tm_hour);
	BCD_TO_BIN(time.tm_mday);
	BCD_TO_BIN(time.tm_mon);
	BCD_TO_BIN(time.tm_year);
	--time.tm_mon;
	startup_time = kernel_mktime(&time);
}

static long mem_end = 0;
static long buf_mem_end = 0;
static long main_mem_start = 0;

struct drive_info {		/* hd arguments table */
	char dummy[32];
} drive_info;

void main (void) {
	/* setup and enable interrupts */
	ROOT_DEV = ORIG_ROOT_DEV;		/* fs/super.c */
	drive_info = DRIVE_INFO;
	mem_end = (1 << 20) + (EXT_MEM_K << 10);
	mem_end &= 0xfffff000;			/* ignire the memory less than a page (4kB) */
	
	if (mem_end > 16*1024*1024)
		mem_end = 16*1024*1024;
	if (mem_end > 12*1024*1024)
		buf_mem_end = 4*1024*1024;
	else if (mem_end > 6*1024*1024)
		buf_mem_end = 2*1024*1024;
	else 
		buf_mem_end = 1*1024*1024;
	main_mem_start = buf_mem_end;
	
	mem_init(main_mem_start, mem_end);
	trap_init();			/* kernel/traps.c */
	blk_dev_init();		/* kernel/blk_dev/rw_blk.c */
	chr_dev_init();		/* kernel/chr_dev/tty_io.c */
	tty_init();				/* kernel/chr_dev/tty_io.c */
	time_init();
	sched_init();			/* kernel/sched.c */
	buf_init(buf_mem_end);		/* fs/buffer.c */
	hd_init();				/* kernel/blk_dev/hd.c */
	floppy_init();			/* kernel/blk_dev/floppy.c */
	sti();
	mov_to_usr();			/* include/asm/system.h */
	if (fork() == 0) init();	/* init in task1 */
	for (;;) pause();
}

static int printf(const char *fmt, ...) {
	va_list args;
	int i;
	va_start(args, fmt);
	write(1, printbuf, i = vsprintf(printbuf, fmt, args));	/* 1 -- stdout */
	va_end(args);
	return i;
}

static char *argv_rc = { "/bin/sh", NULL };
static char *envp_rc = { "HOME=/", NULL };
static char *argv = { "-/bin/sh", NULL };
static char *envp = { "HOME=/usr/root", NULL };

void init(void) {
	int pid, i;
	setup((void *)&drive_info);
	
	/* 0 -- /dev/tty0, 1 -- stdout, 2 -- stderr */
	(void) open("/dev/tty0", O_RDWR, 0);
	(void) dup(0);
	(void) dup(0);
	printf("%d buffers = %d bytes buffer space\n\r", NR_BUFFERS, NR_BUFFERS*BLOCK_SIZE);
	printf("Free memory: %d bytes\n\r", mem_end-main_mem_start);
	
	if ((pid = fork()) == 0) {		/* task2 */
		close(0);
		if (open("/etc/rc", O_RDONLY, 0))
			_exit(1);		/* operation is not allowed */
		execve("/bin/sh", argv_rc, envp_rc);
		_exit(2);			/* file or directory does not exist */
	}
	if (pid > 0) while (pid != wait(&i)) continue;
	
	/* a huge loop */
	while (1) {
		if ((pid = fork()) < 0) {
			printf("Fork failed in init\n\r");
			continue;
		}
		if (pid == 0) {
			close(0); close(1); close(2); 
			setsid();
			(void) open("/dev/tty0", O_RDWR, 0);
			(void) dup(0);
			(void) dup(0);
			_exit(execve("/bin/sh", argv, envp));
		}
		while (1) if (pid == wait(&i)) break;
		printf("\n\rchild %d died with code %04x\n\r", pid, i);
		sync();			/* flush the buffer */
	}
	_exit(0);
}