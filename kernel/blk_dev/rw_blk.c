/*
 * Mirix 1.0/kernel/blk_dev/rw_blk.c
 * (C) 2022 Miris Lee
 */

#include <errno.h>
#include <mirix/sched.h>
#include <mirix/kernel.h>
#include <asm/sytem.h>
#include "blk.h"

struct request request[NR_REQUEST];

struct task_struct *wait_head = NULL;

struct blk_dev_struct blk_dev[NR_BLK_DEV] = {
    { NULL, NULL },     /* 0 -- null */
    { NULL, NULL },     /* 1 -- mem */
    { NULL, NULL },     /* 2 -- floppy */
    { NULL, NULL },     /* 3 -- hd */
    { NULL, NULL },     /* 4 -- ttyx */
    { NULL, NULL },     /* 5 -- tty */
    { NULL, NULL },     /* 6 -- lp */
};

static inline void lock_buffer(struct buffer_head *head) {
    cli();
    while (head->b_lock) sleep_on(&head->b_wait);
    head->b_lock = 1;
    sti();
}

static inline void unlock_buffer(struct buffer_head *head) {
	if (!head->b_lock)
		printk("rw_blk.c: buffer not locked\n");
	head->b_lock = 0;
	wake_up(&head->b_wait);
}

static void add_request(struct blk_dev_struct *dev, struct request *req) {
    struct request *tmp;

    req->next = NULL;
    cli();
    if (req->head) req->head->b_dirt = 0;

    if (!(tmp = dev->current_request)) {
        dev->current_request = req;
        sti();
        (dev->request_func)();
        return;
    }
    /* add 'req' to the request queue */
    /* TODO: optimize this with thw elevator algorithm */
    for (; tmp->next; tmp = tmp->next) continue;
    req->next = tmp->next;
    tmp->next = req;
    sti();
}

/* make a requset and add it to the queue */
static void make_request(int major, int cmd, struct buffer_head *head) {
    struct request *req;
    int ahead;

    if (ahead = (cmd == READA || cmd == WRITEA)) {
        if (head->b_lock) return;
        if (cmd == READA) 
            cmd = READ;
        else 
            cmd = WRITE;
    }
    if (cmd != READ && cmd != WRITE)
        panic("Bad blk_dev command (R/W/RA/WA)");
    lock_buffer(head);
    if ((cmd == READ && head->b_update) || (cmd == WRITE && !head->b_dirt)) {
        unlock_buffer(head);
        return;
    }

loop:
    /* change the order of the requests in the queue */
    if (cmd == READ)
        req = request + NR_REQUEST;
    else 
        req = request + NR_REQUEST * 2 / 3;

    while (--req >= request) {
        if (req->dev == -1)     /* free */
            break;
    }
    if (req < request) {    /* none is free */
        if (ahead) {
            unlock_buffer(head);
            return;
        }
        sleep_on(&wait_head);
        goto loop;
    }

    /* we have found a free entry so far */
    req->dev = head->b_dev;
    req->cmd = cmd;
    req->errors = 0;
    req->sector = head->b_nr_blk << 1;  /* 1 block = 2 sectors */
    req->nr_sect = 2;
    req->buffer = head->b_data;
    req->waiting = NULL;
    req->head = head;
    req->next = NULL;
    add_request(blk_dev + major, req);
}

void rw_blk(int cmd, struct buffer_head *head) {
    unsigned int major;

    if ((major = MAJOR(head->b_dev)) >= NR_BLK_DEV 
        || !(blk_dev[major].request_func)) {
        printk("Trying to read nonexisting blk_dev\n\r");
        return;
    }
    make_request(major, cmd, head);
}

void blk_dev_init(void) {
    int i;
    for (i = 0; i < NR_REQUEST; ++i) {
        request[i].dev = -1;
        request[i].next = NULL;
    }
}