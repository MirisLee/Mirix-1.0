/* 
 * Mirix 1.0/kernel/signal.c
 * (C) 2022 Miris Lee 
 */

#include <mirix/sched.h>
#include <mirix/kernel.h>
#include <asm/segment.h>
#include <signal.h>

volatile void do_exit(int err_code);

int sys_sgetmask(void) {
    return current->blocked;
}

int sys_ssetmask(int new) {
    int original = current->blocked;
    current->blocked = new & ~(1 << (SIGKILL - 1));
    return original;
}

static inline void save_original(char *src, char *dest) {
    int i;
    verify_area(dest, sizeof(struct sigaction));
    for (i = 0; i < sizeof(struct sigaction); ++i) 
        put_fs_byte(*(src++), dest++);
}

static inline void get_new(char *src, char *dest) {
    int i;
    for (i = 0; i < sizeof(struct sigaction); ++i) 
        *(dest++) = get_fs_byte(src++);
}

int sys_signal(int sig, long handler, long restorer) {
    struct sigaction tmp;
    if (sig < 1 || sig > 32 || sig == SIGKILL) return -1;
    tmp.sa_handler = (void (*)(int))handler;
    tmp.sa_mask = 0;
    tmp.sa_flags = SA_ONESHOT | SA_NOMASK;
    tmp.sa_restorer = (void (*)(void))restorer;
    handler = (long)current->sigaction[sig - 1].sa_handler;
    current->sigaction[sig - 1] = tmp;
    return handler;
}

int sys_sigaction(int sig, const struct sigaction *action, 
    struct sigaction *original) {

    struct sigaction tmp;
    if (sig < 1 || sig > 32 || sig == SIGKILL) return -1;
    tmp = current->sigaction[sig - 1];
    get_new((char *)action, (char *)(current->sigaction + sig - 1));
    if (original)
        save_original((char *)&tmp, (char *)original);
    if (current->sigaction[sig - 1].sa_flags & SA_NOMASK)
        current->sigaction[sig - 1].sa_mask = 0;
    else
        current->sigaction[sig - 1].sa_mask |= (1 << (sig - 1));
    return 0;
}

void do_signal(long sig, long eax, long ebx, long ecx, long edx,
    long fs, long es, long ds, long eip, long cs, long eflags,
    unsigned long *esp, long ss) {

    unsigned long handler;
    long original_eip = eip;
    struct sigaction *action = current->sigaction + sig - 1;
    int stack_offset
    unsigned long *tmp_esp;

    handler = (unsigned long)action->sa_handler;
    if (handler == 1) return;
    if (!handler) 
        if (sig == SIGCHLD) 
            return;
        else 
            do_exit(1 << sig);
    if (action->sa_flags & SA_ONESHOT) action->sa_handler = NULL;

    *(&eip) = handler;
    stack_offset = (action->sa_flags & SA_NOMASK)? 7: 8;
    *(&esp) -= stack_offset;
    verify_area(esp, stack_offset * 4);
    tmp_esp = esp;
    put_fs_long((long)action->sa_restorer, tmp_esp++);
    put_fs_long(sig, tmp_esp++);
    if (!(action->sa_flags & SA_NOMASK))
        put_fs_long(current->blocked), tmp_esp++);
    put_fs_long(eax, tmp_esp++);
    put_fs_long(ecx, tmp_esp++);
    put_fs_long(edx, tmp_esp++);
    put_fs_long(eflags, tmp_esp++);
    put_fs_long(original_eip, tmp_esp++);
    current->blocked |= action->sa_sa_mask;
}