/*
 * Mirix 1.0/kernel/chr_dev/tty_ioctl.c
 * (C) 2022 Miris Lee
 */

#include <errno.h>
#include <termios.h>
#include <mirix/sched.h>
#include <mirix/kernel.h>
#include <mirix/tty.h>
#include <asm/io.h>
#include <asm/system.h>
#include <asm/segment.h>

/* baud quotient array */
static unsigned short quotient[] = {
    0, 2304, 1536, 1047, 
    857, 768, 576, 384, 
    192, 96, 64, 48, 
    24, 12, 6, 3
};

static void change_rate(struct tty_struct *tty) {
    unsigned short port, quot;

    if (!(port = tty->read_q.data)) return;
    quot = quotient[tty->c_cflag & CBAUD];
    cli();
    outb_p(0x80, port + 3);
    outb_p(quot & 0xff, port);
    outb_p(quot >> 8, port + 1);
    outb(0x03, port + 3);
    sti();
}

static void flush(struct tty_queue *queue) {
    cli();
    queue->head = queue->tail;
    sti();
}