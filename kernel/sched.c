/*
 * Mirix 1.0/kernel/sched.c
 * (C) 2022 Miris Lee
 */

#include <mirix/sched.h>
#include <mirix/kernel.h>
#include <mirix/sys.h>
#include <mirix/floppy_arg.h>
#include <asm/system.h>
#include <asm/io.h>
#include <asm/segment.h>
#include <signal.h>

#define _S(sig) (1 << ((sig) - 1)
#define _BLOCKABLE (~(_S(SIGKILL) | _S(SIGSTOP)))

void show_task(int nr, struct task_struct *p) {
    int i = 0, j = 4096 - sizeof(struct task_struct);
    printk("%d: pid = %d, state = %d, ", nr, p->pid, p->state);
    while (i < j && !((char *)(p + 1))[i]) i++;
    printk("%d (of %d) bytes free in kernel stack\n\r", i, j);
}

void show_stat(void) {
    int i;
    for (i = 0; i < NR_TASKS; ++i)
        if (task[i]) show_task(i, task[i]);
}

#define LATCH (1193180 / HZ)

extern int timer_interrupt(void);   /* kernel/syscall.asm */
extern int system_call(void);       /* kernel/syscall.asm */

union task_union {
    struct task_struct task;
    char stack[PAGE_SIZE];
}

static union task_union init_task = { INIT_TASK, };

volatile long jiffies = 0;

long startup_time = 0;
struct task_struct *current = &(init_task.task);
struct task_struct *tasks[NR_TASKS] = { &(init_task.task), };
long user_stack[PAGE_SIZE >> 2];

/* boot/head.asm */
struct {
    long *a;
    short b;
} stack_start = { &user_stack[PAGE_SIZE >> 2], 0x10 };

void schedule(void) {
    int i, next, c;
    struct task_struct **p;

    for (p = &LAST_TASK; p > &FIRST_TASK; --p) {
        if (*p) {
            if ((*p)->alarm && (*p)->alarm < jiffies) {
                (*p)->signal |= (1 << (SIGALRM - 1));
                (*p)->alarm = 0;
            }
            if ((*p)->state == TASK_INTERRUPTIBLE
                && ((*p)->signal & ~((*p)->blocked & _BLOCKABLE)))
                (*p)->state = TASK_RUNNING;
        }
    }

    while (1) {
        i = NR_TASKS;
        next = 0;
        c = -1;
        p = &task[NR_TASKS];

        while (--i) {
            if (!*--p) continue;
            if ((*p)->state == TASK_RUNNING && (*p)->counter > c) {
                c = (*p)->counter;
                next = i;
            }
        }
        if (c) break;
        for (p = &LAST_TASK; p > &FIRST_TASK; --p) {
            if (*p) 
                (*p)->counter = ((*p)->counter >> 1) + (*p)->priority;
        }
    }
    switch_to(next);
}

int sys_pause(void) {
    current->state = TASK_INTERRUPTIBLE;
    schedule();
    return 0;
}

void sleep_on(struct task_struct **p) {
    struct task_struct *tmp;
    if (!p) return;
    if (current == &(init_task.task)) panic("task[0] trying to sleep");
    tmp = *p;
    *p = current;
    current->state = TASK_UNINTERRUPTIBLE;
    schedule();
    if (tmp) tmp->state = 0;
}

void interruptible_sleep_on(struct task_struct **p) {
    struct task_struct *tmp;
    if (!p) return;
    if (current == &(init_task.task)) panic("task[0] trying to sleep");
    tmp = *p;
    *p = current;
    while (1) {
        current->state = TASK_INTERRUPTIBLE;
        schedule();
        if (!*p || *p == current) break;
        (**p).state = 0;
    }
    *p = tmp;
    if (tmp) tmp->state = 0;
}

void wake_up(struct task_struct **p) {
    if (p && *p) {
        (**p).state = 0;
        *p = NULL;
    }
}

#define TIME_REQUESTS 64

static struct timer_list {
    long jiffies;
    void (*func)(void);
    struct timer_list *next;
} timer_list[TIME_REQUESTS], *next_timer = NULL;

void add_timer(long jiffies, void (*func)(void)) {
    struct timer_list *p;
    if (!func) return;
    cli();
    if (jiffies <= 0) {
        (func)();
    } else {
        for (p = timer_list; p < timer_list + TIME_REQUESTS; ++p)
            if (!p->func) break;
        if (p >= timer_list + TIME_REQUEST)
            panic("No more time requests free");

        p->func = func;
        p->jiffies = jiffies;
        p->next = next_timer;
        next_timer = p;

        while (p->next && p->next->jiffies < p->jiffies) {
			p->jiffies -= p->next->jiffies;
			func = p->func;
			p->func = p->next->func;
			p->next->func = func;
			jiffies = p->jiffies;
			p->jiffies = p->next->jiffies;
			p->next->jiffies = jiffies;
			p = p->next;
		}
    }
    sti();
}

static struct task_struct * wait_motor[4] = { NULL, NULL, NULL, NULL };
static int mon_timer[4]={ 0, 0, 0, 0 };
static int moff_timer[4]={ 0, 0, 0, 0 };
unsigned char cur_DOR = 0x0c;

int ticks_to_floppy_on(unsigned int minor) {
    extern unsigned char sel;       /* kernel/blk_dev/floppy.c */
    unsigned char mask = 0x10 << minor;

    if (minor > 3) panic("floppy_on: minor > 3");
    moff_timer[minor] = 10000;
    cli();
    if (!sel) {
        mask &= 0xfc;
        mask |= minor;
    }
    if (mask != cur_DOR) {
        outb(mask, FLOPPY_DOR);
        if ((mask ^ cur_DOR) & 0xf0)
            mon_timer[minor] = HZ / 2;
        else if (mon_timer[minor] < 2)
            mon_timer[minor] = 2;
        cur_DOR = mask;
    }
    sti();
    return mon_timer[minor];
}

void floppy_on(unsigned int minor) {
    cli();
    while (ticks_to_floppy_on(minor)) sleep_on(wait_motor + minor);
    sti();
}

void floppy_off(unsigned int minor) {
    moff_timer[minor] = 3 * HZ;
}

void do_floppy_timer(void) {
    int i;
    unsigned char mask = 0x10;

    for (i = 0; i < 4; ++i, mask <<= 1) {
        if (!(mask & cur_DOR)) continue;
        if (mon_timer[i]) {
            if (!--mon_timer[i]) wake_up(wait_motor + i);
        } else if (!moff_timer[i]) {
            cur_DOR &= ~mask;
            outb(cur_DOR, FLOPPY_DOR);
        } else {
            moff_timer[i]--;
        }
    }
}

void do_timer(long cpl) {
    extern int beepcount;
    extern void beepstop(void);

    if (beepcount)
        if (!--beepcount) beepstop();

    if (cpl) current->utime++;
    else current->ktime++;

    if (next_timer) {
        next_timer->jiffies--;
        while (next_timer && next_timer->jiffies <= 0) {
            void (*func)(void);
            func = next_timer->func;
            next_timer->func = NULL;
            next_timer = next_timer->next;
            (func)();
        }
    }

    if (cur_DOR & 0xf0)
        do_floppy_timer();
    if ((--current->counter) > 0) return;
    current->counter = 0;
    if (!cpl) return;
    schedule();
}

int sys_alarm(long sec) {
    int original = current->alarm;
    if (original)
        original = (original - jiffies) / HZ;
    current->alarm = (sec > 0)? (jiffies + HZ * sec): 0;
    return original;
}

int sys_getpid(void) {
    return current->pid;
}

int sys_getppid(void) {
    return current->parent;
}

int sys_getuid(void) {
    return current->uid;
}

int sys_geteuid(void) {
    return current->euid;
}

int sys_getgid(void) {
    return current->gid;
}

int sys_getegid(void) {
    return current->egid;
}

int sys_nice(long increment) {
    if (current->priority - increment > 0)
        current->priority -= increment;
    return 0;
}

void sched_init(void) {
    int i;
    struct desc_struct *desc;

    if (sizeof(struct sigaction) != 16)
        panic("struct sigaction must be 16 bytes");
    set_tss_desc(gdt + FIRST_TSS_ENTRY, &(init_task.task.tss));
    set_ldt_desc(gdt + FIRST_LDT_ENTRY, &(init_task.task.ldt));
    desc = gdt + 2 + FIRST_TSS_ENTRY;
    for (i = 0; i < NR_TASKS; ++i) {
        task[i] = NULL;
        desc->a = desc->b = 0;
        desc++;
        desc->a = desc->b = 0;
        desc++;
    }

    __asm__("pushfl; andl $0xffffbffff, (%esp); popfl");    /* clear NT */
    ltr(0);
    lldt(0);
    /* init 8253 */
    outb_p(0x36, 0x43);
    outb_p(LATCH & 0xff, 0x40);
    outb_p(LATCH >> 8, 0x40);
    set_int_gate(0x20, &timer_interrupt);
    outb(inb_p(0x21) & 0xfe, 0x21);     /* allow timer interrupt */
    set_system_gate(0x80, &system_call);
}