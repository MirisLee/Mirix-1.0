/*
 * Mirix 1.0/kernel/printk.c
 * (C) 2022 Miris Lee
 */

#include <stdarg.h>
#include <stddef.h>
#include <mirix/kernel.h>

static char print_buf[1024];

extern int vsprintf(char *buf, const char *fmt, va_list args);

int printk(const char *fmt, ...) {
    va_list args;
    int i;
    va_start(args, fmt);
    i = vsprintf(print_buf, fmt, args);
    va_end(args);
    __asm__(
        "push %%fs \n\t"
        "push %%ds \n\t"
        "pop &&fs \n\t"
        "pushl %0 \n\t"
        "pushl $_print_buf \n\t"
        "pushl $0 \n\t"
        "call _tty_write \n\t"  /* kernel/chr_dev/tty_io.c */
        "addl $8, %%esp \n\t"
        "popl %0 \n\t"
        "pop %%fs"
        :: "r" (i): "ax", "cx", "dx"
    );
    return i;
}