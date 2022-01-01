/* 
 * Mirix 1.0/kernel/blk_dev/blk.h
 * (C) 2022 Miris Lee
 */

#ifndef BLK_H_
#define BLK_H_

#define NR_BLK_DEV	7
#define NR_REQUEST	16

/* request for both blk_dev and paging */
struct request {
	int dev;		/* -1 -- no request */
	int cmd;		/* R/W */
	int errors;
	unsigned long sector;
	unsigned long nr_sect;
	char *buffer;
	struct task_struct *waiting;
	struct buffer_head *head;
	struct request *next;
};

struct blk_dev_struct {
	void (*request_func)(void);
	struct request *current_request;
};

extern struct blk_dev_struct blk_dev[NR_BLK_DEV];
extern struct request request[NR_REQUEST];
extern struct task_struct *wait_head;

/* major nr should be defined in the including file */
#ifdef MAJOR_NR

#if (MAJOR_NR == 2)
/* floppy */
#define DEV_NAME		"floppy"
#define DEV_INT		do_floppy
#define DEV_REQUEST	do_floppy_request
#define DEV_NR(dev)	((dev) & 3)
#define DEV_ON(dev)	floppy_on(DEV_NR(dev))
#define DEV_OFF(dev)	floppy_off(DEV_NR(dev))

#elif (MAJOR_NR == 3)
/* hd */
#define DEV_NAME		"hd"
#define DEV_INT		do_hd
#define DEV_REQUEST	do_hd_request
#define DEV_NR(dev)	(MINOR(dev) / 5)
#define DEV_ON(dev)	
#define DEV_OFF(dev)	

#elif
/* unknown */
#error "unknown block device"

#endif

#define CURRENT		(blk_dev[MAJOR_NR].current_request)
#define CURRENT_DEV	DEV_NR(CURRENT->dev)

void (*DEV_INT)(void) = NULL;
static void (DEV_REQUEST)(void);

extern inline void unlock_buffer(struct buffer_head *head) {
	if (!head->b_lock)
		printk(DEV_NAME ": free buffer being unlocked\n");
	head->b_lock = 0;
	wake_up(&head->b_wait);
}

#endif

#endif