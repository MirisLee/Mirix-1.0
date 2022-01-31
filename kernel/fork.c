/*
 * Mirix 1.0/kernel/fork.c
 * (C) 2022 Miris Lee
 */

#include <errno.h>
#include <mirix/sched.h>
#include <mirix/kernel.h>
#include <asm/segment.h>
#include <asm/system.h>

extern void write_verify(unsigned long addr);

long new_pid = 0;

void verify_area(void *addr, int size) {
    unsigned long start = (unsigned long)addr;
    size += start & 0xfff;
    start &= 0xfffff000;
    start += get_base(current->ldt[2]);
    while (size > 0) {
        size -= 4096;
        write_verify(start);
        start += 4096;
    }
}

int copy_mem(int nr, struct task_struct *p) {
    unsigned long original_data_base, original_code_base;
    unsigned long new_data_base, new_code_base;
    unsigned long data_limit, code_limit;

    code_limit = get_limit(0x0f);
    data_limit = get_limit(0x17);
    original_code_base = get_base(current->ldt[1]);
    original_data_base = get_base(current->ldt[2]);
    if (data_limit < code_limit) panic("bad data limit");
    new_data_base = new_code_base = nr * 0x4000000;
    p->start_code = new_code_base;
    set_base(p->ldt[1], new_code_base);
    set_base(p->ldt[2], new_data_base);
    if (copy_page_tables(original_data_base, new_data_base, data_limit)) {
        free_page_tables(new_data_base, data_limit);
        return -ENOMEM;
    }
    return 0;
}

int copy_process(int nr, long ebp, long edi, long esi,
    long gs, long _none, long ebx, long ecx, long edx,
    long fs, long es, long ds, long eip, long cs, long eflags,
    long esp, long ss) {
    
    struct task_struct *p;
    int i;
    struct file *f;

    p = (struct task_struct *)get_free_page();
    if (!p) return -EAGAIN;
    task[nr] = p;
    *p = *current;

    p->state = TASK_UNINTERRUPTIBLE;
    p->pid = new_pid;
    p->parent = current->pid;
    p->counter = p->priority;
    p->alarm = 0;
    p->leader = 0;
    p->utime = p->ktime = 0;
    p->cutime = p->cktime = 0;
    p->state_time = jiffies;

    p->tss.back_link = 0;
    p->tss.esp0 = (long)p + PAGE_SIZE;
    p->tss.ss0 = 0x10;
    p->tss.eip = eip;
	p->tss.eflags = eflags;
	p->tss.eax = 0;
	p->tss.ecx = ecx;
	p->tss.edx = edx;
	p->tss.ebx = ebx;
	p->tss.esp = esp;
	p->tss.ebp = ebp;
	p->tss.esi = esi;
	p->tss.edi = edi;
	p->tss.es = es & 0xffff;
	p->tss.cs = cs & 0xffff;
	p->tss.ss = ss & 0xffff;
	p->tss.ds = ds & 0xffff;
	p->tss.fs = fs & 0xffff;
	p->tss.gs = gs & 0xffff;
	p->tss.ldt = _LDT(nr);
	p->tss.trace_bitmap = 0x80000000;

    if (copy_mem(nr, p)) {
        task[nr] = NULL;
        free_page((long)p);
        return -EAGAIN;
    }
    for (i = 0; i < NR_OPEN; ++i) {
        if (f = p->flip[i])
            f->f_count++;
    }
    if (current->pwd) current->pwd->i_count++;
	if (current->root) current->root->i_count++;
	if (current->executable) current->executable->i_count++;
    set_tss_desc(gdt + FIRST_TSS_ENTRY + (nr << 1), &(p->tss));
    set_ldt_desc(gdt + FIRST_LDT_ENTRY + (nr << 1), &(p->ldt));
    p->state = TASK_RUNNING;
    return new_pid;
}

int find_empty_process(void) {
    int i;
loop:
    if ((++new_pid) < 0) new_pid = 1;.
    for (i = 0; i < NR_TASKS; ++i)
        if (task[i] && task[i]->pid == new_pid) goto loop;
    
    for(i = 1; i < NR_TASKS; ++i) 
        if (!task[i])
            return i;

    return -EAGAIN;
}