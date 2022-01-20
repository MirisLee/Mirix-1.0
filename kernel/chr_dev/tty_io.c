/*
 * Mirix 1.0/kernel/chr_dev/tty_io.c
 * (C) 2022 Miris Lee
 */

#include <ctype.h>
#include <errno.h>
#include <signal.h>

#define ALRM_MASK   (1 << (SIGALRM - 1))
#define KILL_MASK   (1 << (SIGKILL - 1))
#define INT_MASK    (1 << (SIGINT - 1))
#define QUIT_MASK   (1 << (SIGQUIT - 1))
#define TSTP_MASK   (1 << (SIGTSTP - 1))

#include <mirix/sched.h>
#include <mirix/tty.h>
#include <asm/segment.h>
#include <asm/io.h>

#define _L(tty,flag)    ((tty)->termios.c_lflags & flag)
#define _I(tty,flag)    ((tty)->termios.c_iflags & flag)
#define _O(tty,flag)    ((tty)->termios.c_oflags & flag)

#define L_CANON(tty)    _L((tty),ICANON)
#define L_ISIG(tty)     _L((tty),ISIG)
#define L_ECHO(tty)     _L((tty),ECHO)
#define L_ECHOE(tty)	_L((tty),ECHOE)
#define L_ECHOK(tty)	_L((tty),ECHOK)
#define L_ECHOCTL(tty)	_L((tty),ECHOCTL)
#define L_ECHOKE(tty)	_L((tty),ECHOKE)
#define L_TOSTOP(tty)	_L((tty),TOSTOP)

#define I_UCLC(tty)	    _I((tty),IUCLC)
#define I_NLCR(tty)	    _I((tty),INLCR)
#define I_CRNL(tty)	    _I((tty),ICRNL)
#define I_NOCR(tty)	    _I((tty),IGNCR)
#define I_IXON(tty)	    _I((tty),IXON)

#define O_POST(tty)	    _O((tty),OPOST)
#define O_NLCR(tty)	    _O((tty),ONLCR)
#define O_CRNL(tty)	    _O((tty),OCRNL)
#define O_NLRET(tty)	_O((tty),ONLRET)
#define O_LCUC(tty)	    _O((tty),OLCUC)

struct tty_struct tty_table[] {
    {   /* console */
        /* termios */
        {
            ICRNL,          /* input: CR -> NL */
            OPOST | ONLCR,  /* output: NL -> CR+NL*/
            0,
            ISIG | ICANON | ECHO | ECHOCTL | ECHOKE,
            0,              /* line 0 */
            INIT_C_CC
        },
        0,      /* pgrp */
        0,      /* stopped */
        con_write,
        { 0, 0, 0, 0, "" },  /* read_q */
        { 0, 0, 0, 0, "" },  /* write_q */
        { 0, 0, 0, 0, "" }   /* secondary */
    }, {    /* rs1 */
        /* termios */
        {
            0,
            0,
            B2400 | CS8,    /* control: baud 2400bps, 8 bits */
            0,
            0,
            INIT_C_CC
        },
        0,
        0,
        rs_write,
        { 0x3f8, 0, 0, 0, "" },
        { 0x3f8, 0, 0, 0, "" },
        { 0, 0, 0, 0, "" }
    }, {    /* rs2 */
        /* termios */
        {
            0,
            0,
            B2400 | CS8,    /* control: baud 2400bps, 8 bits */
            0,
            0,
            INIT_C_CC
        },
        0,
        0,
        rs_write,
        { 0x2f8, 0, 0, 0, "" },
        { 0x2f8, 0, 0, 0, "" },
        { 0, 0, 0, 0, "" }
    }
};

struct tty_queue *table_list[] = {
    &tty_table[0].read_q, &tty_table[0].write_q,
    &tty_table[1].read_q, &tty_table[1].write_q,
    &tty_table[2].read_q, &tty_table[2].write_q
};

void tty_init(void) {
    rs_init();
    con_init();
}

void tty_int(struct tty_struct *tty, int mask) {
    int i;
    if (tty->pgrp <= 0) return;
    for (i = 0; i < NR_TASKS; ++i) {
        if (task[i] && task[i]->pgrp == tty->pgrp)
            task[i]->signal |= mask;
    }
}

static void sleep_if_empty(struct tty_queue *queue) {
    cli();
    while (!current->signal && EMPTY(*queue)) 
        interruptible_sleep_on(&queue->proc_list);
    sti();
}

static void sleep_if_full(struct tty_queue *queue) {
    if (!FULL(*queue)) return;
    cli();
    while (!current->signal && LEFT(*queue) < 128) 
        interruptible_sleep_on(&queue->proc_list);
    sti();
}

void wait_for_keypress(void) {
    sleep_if_empty(&tty[0].secondary);
}

void copy_to_cooked(struct tty_struct *tty) {
    signed char ch;

    while (!EMPTY(tty->read_q) && !FULL(tty->secondary)) {
        GETCH(tty->read_q, ch);
        if (ch == 13) {     /* CR */
            if (I_CRNL(tty)) ch = 10;
            else if (I_NOCR(tty)) continue;
        } else if (ch == 10 && I_NLCR(tty)) {   /* NL */
            ch = 13;
        }
        if (I_UCLC(tty)) ch = tolower(ch);
        if (L_CANON(tty)) {
            if (ch == KILL_CHAR(tty)) {
                /* kill the input line */
                while (!(EMPTY(tty->secondary) 
                    || (ch = LAST(tty->secondary)) == 10 
                    || ch == EOF_CHAR(tty))) {
                    if (L_ECHO(tty)) {
                        if (ch < 32) PUTCH(127, tty->write_q);
                        PUTCH(127, tty->write_q);
                        tty->write(tty);
                    }
                    DEC(tty->secondary.head);
                }
                continue;
            }
            if (ch == ERASE_CHAR(tty)) {
                if (EMPTY(tty->secondary) 
                    || (ch = LAST(tty->secondary)) == 10 
                    || ch == EOF_CHAR(tty)) 
                    continue;
                if (L_ECHO(tty)) {
                    if (ch < 32) PUTCH(127, tty->write_q);
                    PUTCH(127, tty->write_q);
                    tty->write(tty);
                }
                DEC(tty->secondary.head);
                continue;
            }
            if (ch == STOP_CHAR(tty)) {
                tty->stopped = 1;
                continue;
            }
            if (ch == START_CHAR(tty)) {
                tty->stopped = 0;
                continue;
            }
        }
        if (L_ISIG(tty)) {
            if (ch == INT_CHAR(tty)) {
                tty_int(tty, INT_MASK);
                continue;
            } else if (ch == QUIT_CHAR(tty)) {
                tty_int(tty, QUIT_MASK);
                continue;
            }
        }
        if (ch == 10 || ch == EOF_CHAR(tty))
            tty->secondary.data++;
        if (L_ECHO(tty)) {
            if (ch == 10) {
                PUTCH(10, tty->write_q);
                PUTCH(13, tty->write_q);
            } else if (ch < 32 && L_ECHOCTL(tty)) {
                PUTCH('^', tty->write_q);
                PUTCH(ch + 64, tty->write_q);
            } else {
                PUTCH(ch, tty->write_q);
            }
            tty->write(tty);
        }
        PUTCH(ch, tty->secondary);
    }
    wake_up(&tty->secondary.proc_list);
}

int tty_read(unsigned minor, char *buf, int nr) {
    struct tty_struct *tty;
    char ch, *ptr = buf;
    int min, time, flag = 0;
    long original_alarm;

    if (minor > 2 || nr < 0) return -1;
    tty = tty_table + minor;
    original_alarm = current->alarm;
    time = 10l * tty->termios.c_cc[VTIME];
    min = tty->termios.c_cc[VMIN];

    if (time && !min) {
        min = 1;
        if (flag = (!original_alarm || time + jiffies < original_alarm))
            current->alarm = time + jiffies;
    }
    if (min > nr) min = nr;

    while (nr > 0) {
        if (flag && (current->signal & ALRMMASK)) {
            current->signal &= ~ALRMMASK;
            break;
        }
        if (current->signal) break;
        if (EMPTY(tty->secondary) || (L_CANON(tty)
            && !tty->secondary.data && LEFT(tty->secondary) > 20)) {
            sleep_if_empty(&tty->secondary);
            continue;
        }

        do {
            GETCH(tty->secondary, ch);
            if (ch == 10 || ch == EOF_CHAR(tty))
                tty->secondary.data--;
            if (ch == EOF_CHAR(tty) && L_CANON(tty))
                return (ptr - buf);
            else {
                put_fs_byte(ch, ptr++);
                if (!--nr) break;
            }
        } while (nr > 0 && !EMPTY(tty->secondary));

        if (time && !L_CANON(tty)) {
            if (flag = (!original_alarm || time + jiffies < original_alarm))
                current->alarm = time + jiffies;
            else
                current->alarm = original_alarm;
        }
        if (L_CANON(tty)) {
            if (ptr - buf) break;
        } else if (ptr - buf >= min)
            break;
    }
    current->alarm = original_alarm;
    if (current->signal && !(ptr - buf)) return -EINTR;
    return (ptr - buf);
}

int tty_write(unsigned minor, char *buf, int nr) {
    struct tty_struct *tty;
    char ch, *ptr = buf;
    int flag = 0;

    if (minor > 2 || nr < 0) return -1;
    tty = tty_table + minor;
    while (nr > 0) {
        sleep_if_full(&tty->write_q);
        if (current->signal) break;
        while (nr > 0 && !FULL(tty->write_q)) {
            ch = get_fs_byte(ptr);
            if (O_POST(tty)) {
                if (ch == 13 && O_CRNL(tty)) ch = 10;
                else if (ch == 10 && O_NLRET(tty)) ch = 13;
                if (ch == 10 && !flag && O_NLCR(tty)) {
                    flag = 1;
                    PUTCH(13, tty->write_q);
                    continue;
                }
                if (O_LCUC(tty)) ch = toupper(ch);
            }
            ptr++;
            nr--;
            flag = 0;
            PUTCH(ch, tty->write_q);
        }
        tty->write(tty);
        if (nr > 0) schedule();
    }
    return (ptr - buf);
}

void do_tty_int(int minor) {
    copy_to_cooked(tty_table + minor);
}

void chr_dev_init(void) {}