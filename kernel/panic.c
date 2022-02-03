/*
 * Mirix 1.0/kernel/panic.c
 * (C) 2022 Miris Lee
 */

#include <mirix/kernel.h>
#include <mirix/sched.h>

void sys_sync(void);    /* fs/buffer.c */

volatile void panic(const char *str) {
    printk("KERNEL PANIC: %s\n\r", str);
    if (current == task[0])
        printk("swapper task error\n\r");
    else
        sys_sync();
    for (;;);
}