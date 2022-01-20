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

static int get_termios(struct tty_struct *tty, struct termios *termios) {
    int i;
    verify_area(termios, sizeof(*termios));
    for (i = 0; i < (sizeof(*termios)); ++i)
        put_fs_byte(((char *)&tty->termios)[i], (char *)termios + i);
    return 0;
}

static int set_termios(struct tty_struct *tty, struct termios *termios) {
    int i;
    for (i = 0; i < (sizeof(*termios)); ++i) 
        ((char *)&tty->termios)[i] = get_fs_byte((char *)termios + i);
    change_rate(tty);
    return 0;
}

static int get_termio(struct tty_struct *tty, struct termio *termio) {
    int i;
    struct termio tmp;
    verify_area(termio, sizeof(*termio));
    tmp.c_iflag = tty->termios.c_iflag;
    tmp.c_oflag = tty->termios.c_oflag;
    tmp.c_cflag = tty->termios.c_cflag;
    tmp.c_lflag = tty->termios.c_lflag;
    tmp.c_line = tty->termios.c_line;
    for (i = 0; i < NCC; ++i)
        tmp.c_cc[i] = tty->termios.c_cc[i];
    for (i = 0; i < (sizeof(*termio)); ++i)
        put_fs_byte(((char *)&tmp)[i], (char *)termio + i);
}

static int set_termio(struct tty_struct *tty, struct termio *termio) {
    int i;
    struct termio tmp;
    for (i = 0; i < (sizeof(*termio)); ++i) 
        ((char *)&tmp)[i] = get_fs_byte((char *)termio + i);
    *(unsigned short *)&tty->termios.c_iflag = tmp.c_iflag;
    *(unsigned short *)&tty->termios.c_oflag = tmp.c_oflag;
    *(unsigned short *)&tty->termios.c_cflag = tmp.c_cflag;
    *(unsigned short *)&tty->termios.c_lflag = tmp.c_lflag;
    tty->termios.c_line = tmp.c_line;
    for (i = 0; i < NCC; ++i)
        tty->termios.c_cc[i] = tmp.c_cc[i];
    change_rate(tty);
    return 0;
}

static void wait_until_sent(struct tty_struct *tty) {}
static void send_break(struct tty_struct *tty) {}

int tty_ioctl(int dev, int cmd, int arg) {
    struct tty_struct *tty;

    if (MAJOR(dev) == 5) {
        dev = current->tty;
        if (dev < 0) panic("tty_ioctl: dev < 0");
    } else {
        dev = MINOR(dev);
    }
    tty = tty_table + dev;

    switch (cmd) {
        case TCGETS:
            return get_termios(tty, (struct termios *)arg);
        case TCSETSF:
            flush(&tty->read_q);        /* no break */
        case TCSETSW:
            wait_until_sent(tty);       /* no break */
        case TCSETS:
            return set_termios(tty, (struct termios *)arg);
        case TCGETA:
            return get_termio(tty, (struct termio *)arg);
        case TCSETAF:
            flush(&tty->read_q);        /* no break */
        case TCSETAW:
            wait_until_sent(tty);       /* no break */
        case TCSETA:
            return set_termio(tty, (struct termio *)arg);
        case TCSBRK:
            if (!arg) {
                wait_until_sent(tty);
                send_break(tty);
            }
            return 0;
        case TCXONC:
            return -EINVAL;
        case TCFLSH:
            if (arg == 0) {
                flush(&tty->read_q);
            } else if (arg == 1) {
                flush(&tty->write_q);
            } else if (arg == 2) {
                flush(&tty->read_q);
                flush(&tty->write_q);
            } else {
                return -EINVAL;
            }
            return 0;
        case TIOCEXCL:
        case TIOCNXTL:
        case TIOCSCTTY:
            return -EINVAL;
        case TIOCGPGRP:
            verify_area((void *)arg, 4);
            put_fs_long(tty->pgrp, (unsigned long *)arg);
            return 0;
        case TIOCSPGRP:
            tty->pgrp = get_fs_long((unsigned long *)arg);
            return 0;
        case TIOCOUTQ:
            verify_area((void *)arg, 4);
            put_fs_long(CHARS(tty->write_q), (unsigned long *)arg);
            return 0;
        case TIOCINQ:
            verify_area((void *)arg, 4);
            put_fs_long(CHARS(tty->secondary), (unsigned long *)arg);
            return 0;
        default:
            return -EINVAL; 
    }
}