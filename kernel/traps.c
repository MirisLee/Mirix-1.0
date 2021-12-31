/* 
 * Mirix 1.0/kernel/traps.c
 * (C) 2021 Miris Lee
 */

#include <string.h>
#include <mirix/head.h>
#include <mirix/sched.h>
#include <mirix/kernel.h>
#include <asm/system.h>
#include <asm/segment.h>
#include <asm/io.h>

#define get_segment_byte(seg,addr) ({ \
	register char __res; \
	__asm__( \
		"push %%fs				;" \
		"mov %%ax, %%fs		;" \
		"movb %%fs:%2, %%al	;" \
		"pop %%fs" \
		: "=a"(__res) \
		: ""(seg), "m"(*(addr))); \
})

#define get_segment_dword(seg,addr) ({ \
	register unsigned long __res; \
	__asm__( \
		"push %%fs				;" \
		"mov %%ax, %%fs		;" \
		"movl %%fs:%2, %%al	;" \
		"pop %%fs" \
		: "=a"(__res) \
		: ""(seg), "m"(*(addr))); \
})

#define _fs() ({ \
	register unsigned short __res; \
	__asm__( \
		"mov %%fs, %%ax"
		: "=a"(__res): ); \
})

int do_exit(long code);		/* kernel/exit.c */
void page_exception(void);		/* mm/page.asm */

void divide_error(void);
void debug(void);
void nmi(void);
void int3(void);
void overflow(void);
void bounds(void);
void invalid_op(void);
void device_not_available(void);
void double_fault(void);
void coproccesor_segment_overrun(void);
void invalid_TSS(void);
void segment_not_present(void);
void stack_segment(void);
void general_protection(void);
void page_fault(void);
void coprocessor_error(void);
void reserved(void);

static void die(char *str, long p_esp, long nr) {
	long *esp = (long *)p_esp;
	int i;
	
	printk("%s: %04x\n\r", str, nr & 0xffff);
	printk("eip: \t%04x:%p\neflags: \t%p\nesp: \t%04x:%p\n", 
		esp[1], esp[0], esp[2], esp[4], esp[3]);
	printk("fs: %4x\n", _fs());
	printk("base: %p, limit: %p\n", get_base(current->ldt[1]), get_limit(0x17));
	if (esp[4] == 0x17) {
		printk("stack: ");
		for (i = 0; i < 4; ++i) printk("%p ", get_segment_dword(0x17, i + (long *)esp[3]));
		printk("\n");
	}
	str(i);
	printk("pid: %d, proccess nr: %d\n\r", current->pid, 0xffff & i);
	for (i = 0; i < 10; ++i) 
		printk("%02x ", get_segment_byte(esp[1], (i + (char *)esp[0])) & 0xff);
	printk("\n\r");
	do_exit(11);
}

void do_divide_error(long esp, long err_code) {
	die("divide error", esp, err_code);
}

void do_debug(long esp, long err_code) {
	die("debug", esp, err_code);
}

void do_nmi(long esp, long err_code) {
	die("nmi", esp, err_code);
}

void do_int3(long *esp, long err_code, 
	long fs, long es, long ds, 
	long ebp, long esi, long edi, 
	long edx, long ecx, long ebx, long eax) {
	int tr;
	__asm__("str %%ax": "=a"(tr): ""(0));
	printk("eax\t\tebx\t\tecx\t\tedx\n\r");
	printk("%8x\t%8x\t%8x\t%8x\n\r", eax, ebx, ecx, edx);
	printk("esi\t\tedi\t\tebp\t\tesp\n\r");
	printk("%8x\t%8x\t%8x\t%8x\n\r", esi, edi, ebp, (long)esp);
	printk("ds\tes\tfs\ttr\n\r");
	printk("%4x\t%4x\t%4x\t%4x\n\r", ds, es, fs, tr);
	printk("eip: %8x cs: %4x eflags: %8x\n\r", esp[0], esp[1], esp[2]);
}

void do_overflow(long esp, long err_code) {
	die("overflow", esp, err_code);
}

void do_bounds(long esp, long err_code) {
	die("bounds", esp, err_code);
}

void do_invalid_op(long esp, long err_code) {
	die("invalid operand", esp, err_code);
}

void do_device_not_available(long esp, long err_code) {
	die("device not available", esp, err_code);
}

void do_double_fault(long esp, long err_code) {
	die("double fault", esp, err_code);
}

void do_coproccesor_segment_overrun(long esp, long err_code) {
	die("coprocessor segment overrun", esp, err_code);
}

void do_invalid_TSS(long esp, long err_code) {
	die("invalid TSS", esp, err_code);
}

void do_segment_not_present(long esp, long err_code) {
	die("segment not present", esp, err_code);
}

void do_stack_segment(long esp, long err_code) {
	die("stack segment", esp, err_code);
}

void do_general_protection(long esp, long err_code) {
	die("general protection", esp, err_code);
}

void do_coprocessor_error(long esp, long err_code) {
	die("coprocessor error", esp, err_code);
}

void do_reserved(long esp, long err_code) {
	die("reserved (15, 17~47) error", esp, err_code);
}

void trap_init(void) {
	int i;
	
	set_trap_gate(0, &divide_error);
	set_trap_gate(1, &debug);
	set_trap_gate(2, &nmi);
	set_system_gate(3, &int3);
	set_system_gate(4, &overflow);
	set_system_gate(5, &bounds);
	set_trap_gate(6, &invalid_op);
	set_trap_gate(7, &device_not_available);
	set_trap_gate(8, &double_fault);
	set_trap_gate(9, &coprocessor_segment_overrun);
	set_trap_gate(10, &invalid_TSS);
	set_trap_gate(11, &segment_not_present);
	set_trap_gate(12, &stack_segment);
	set_trap_gate(13, &general_protection);
	set_trap_gate(14, &page_fault);
	set_trap_gate(15, &reserved);
	set_trap_gate(16, &coprocessor_error);
	for (i = 17; i < 48; ++i) 	set_trap_gate(i, &reserved);
	
	outb_p(inb_p(0x21) & 0xfb, 0x21);	/* master 8259A, IRQ2 allowed */
	outb(intb_p(0xa1) & 0xff, 0xa1);	/* slave 8259A, all ingnored */
}