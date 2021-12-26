/* 
 * Mirix 1.0/mm/mm.c 
 * (C) 2021 Miris Lee
 */

#include <asm/system.h>
#include <mirix/sched.h>
#include <mirix/head.h>
#include <mirix/kernel.h>

/* flush the page cache */
#define invalidate() \
	__asm__("movl %%eax, %%cr3"::"a"(0))
	
#define LOW_MEM 		0x100000
#define PAGING_MEM 	(15*1024*1024)
#define PAGING_PAGE	(PAGING_MEM >> 12)
#define MAP_NR(addr)	(((addr) - LOW_MEM) >> 12)
#define USED_FLAG		100
	
#define PG_DIR(addr) \
	(unsigned long *)(((addr) >> 20) & 0xffc)
#define PG_TABLE(dir) \
	(unsigned long *)(*(dir) & 0xfffff000)

static long HIGH_MEM = 0;
	
#define copy_page(src,dest) \
	__asm__(
		"cld; rep; movsl"
		::"S"(src), "D"(dest), "c"(1024)
		:"cx", "di", "si")

/* byte map of page mapping */
static unsigned char mem_map[PAGING_PAGE] = { 0, };

/* get physical address of the last free page */
unsigned long get_free_page(void) {
	register unsigned long __res asm("ax");
	__asm__(
		"std; repne; scasb	\n\t"
		"jne 1f					\n\t"
		"movb $1, 1(%%edi)	\n\t"
		"sall $12, %%ecx		\n\t"
		"addl %2, %%ecx		\n\t"
		"movl %%ecx, %%edx	\n\t"
		"movl $1024, %%ecx	\n\t"
		"leal 4092(%%edx), %%edi	\n\t"
		"rep; stosl				\n\t"
		"movl %%edx, %%eax	\n"
		"1:"
		: "=a"(__res)
		: ""(0), "i"(LOW_MEM), "c"(PAGING_PAGE), "D"(mem_map+PAGING_PAGE-1)
		: "di", "cx", "dx");
	return __res;
}

/* free a page of memory at physical address 'addr' */
void free_page(unsigned long addr) {
	if (addr < LOW_MEM) return;
	if (addr >= HIGH_MEM) panic("trying to free nonexisting page");
	addr -= LOW_MEM;
	addr >>= 12;			/* get the page index */
	if (mem_map[addr]--) return;
	mem_map[addr] = 0;
	panic("trying to free free page");
}

/* free a continuous block of page tables */
int free_page_tables(unsigned long start, unsigned long size) {
	unsigned long *pg_table;
	unsigned long *dir, nr;
	
	if (start & 0x3fffff) panic("free_page_tables called with wrong alignment");
	if (!start) panic("trying to free up swapper memory space");
	size = (size + 0x3fffff) >> 22;	/* size in page tables */
	dir = PG_DIR(start);
	for (; size-- > 0; ++dir) {
		if (!(*dir & 1)) continue;		/* P=0 */
		pg_table = PG_TABLE(dir);
		for (nr = 0; nr < 1024; ++nr) {
			if (*pg_table & 1) free_page(*pg_table & 0xfffff000);	/* P=1 */
			*pg_table = 0;
			++pg_table;
		}
		free_page(*dir & 0xfffff000);
		*dir = 0;
	}
	invalidate();
	return 0;
}

/* copy a range of linear addresses by copying pages */
int copy_page_tables(unsigned long src, unsigned long dest, long size) {
	unsigned long *src_pg_table, *dest_pg_table;
	unsigned long this_page;
	unsigned long *src_dir, *dest_dir;
	unsigned long nr;
	
	if ((src & 0x3fffff) || (dest & 0x3fffff)) 
		panic("copy_page_tables called with wrong alignment");
	src_dir = PG_DIR(src);
	dest_dir = PG_DIR(dest);
	size = ((unsigned)(size + 0x3fffff)) >> 22;
	
	for (; size-- > 0; ++src_dir, ++dest_dir) {
		if (*dest_dir & 1) panic("copy_page_tables: already exist");	/* P=1 */
		if (!(*src_dir & 1)) continue;	/* P=0 */
		src_pg_table = PG_TABLE(src_dir);
		if (!(dest_pg_table = (unsigned long *)get_free_page())) return -1;	/* out of memory */
		*dest_dir = ((unsigned long)dest_pg_table) | 7;	/* 7 -- usr, R/W, Present */
		nr = (src == 0)? 0xa0: 1024;
		for (; nr-- > 0; ++src_pg_table; ++dest_pg_table) {
			this_page = *src_pg_table;
			if (!(this_page & 1)) continue;
			this_page &= ~2;	/* reset R/W, read only */
			*dest_pg_table = this_table;
			
			if (this_page > LOW_MEM) {
				*src_pg_table = this_page;
				this_page -= LOW_MEM;
				this_page >>= 12;
				mem_map[this_page]++;
			}
		}
	}
	invalidate();
	return 0;
}

/* put a page in memory at address 'addr' */
unsigned long put_page(unsigned long pg, unsigned long addr) {
	unsigned long tmp, *pg_table;
	
	if (pg < LOW_MEM || pg >= HIGH_MEM) 
		printk("Trying to put page %p at %p\n", pg, addr);
	if (mem_map[(pg - LOW_MEM) >> 12] != 1) 
		printk("mem_map disagrees with %p at %p\n", pg, addr);
	pg_table = PG_DIR(addr);
	
	if (*pg_table & 1) {
		pg_table = PG_TABLE(pg_table);
	} else {
		if (!(tmp = get_free_page())) return 0;
		*pg_table = tmp | 7;
		pg_table = (unsigned long *)tmp;
	}
	pg_table[(addr >> 12) & 0x3ff] = pg | 7;
	return pg;
}

/* un-write protected */
void un_wp_page(unsigned long *entry) {
	unsigned long old, new;
	old = *entry & 0xfffff000;
	if (old >= LOW_MEM && mem_map[MAP_NR(old)] == 1) {
		*entry |= 2;
		invalidate();
		return;
	}
	if (!(new = get_free_page())) panic("out of memory");
	if (old >= LOW_MEM) mem_map[MAP_NR(old)]--;
	*entry = new | 7;
	invalidate();
	copy_page(old, new);
}

/* copy a shared page when writing */
void do_wp_page(unsigned long err_code, unsigned long addr) {
	un_wp_page((unsigned long *)
		(((addr >> 10) & 0xffc) + PG_TABLE(PG_DIR(addr))));
}

void write_verify(unsigned long addr) {
	unsigned long page;
	
	if (!((page = *PG_DIR(addr)) & 1)) return;
	page &= 0xfffff000;
	page += ((addr >> 10) & 0xffc);
	
	if ((*(unsigned long *)page & 3) == 1)	/* non-writeable, present */
		un_wp_page((unsigned long *)page);
	return;
}

void get_empty_page(unsigned long addr) {
	unsigned long tmp;
	if (!(tmp = get_free_page()) || !put_page(tmp, addr)) {
		free_page(tmp);
		panic("out of memory");
	}
}

/* share the page at address 'addr' in the task 'p' with the current task */
static int try_to_share(unsigned long addr, struct task_struct *p) {
	unsigned long src, dest, src_pg, dest_pg, phys_addr;
	
	/* get the src and dest page directory entry */
	src_pg = dest_pg = PG_DIR(addr);
	src_pg += PG_DIR(p->start_code);
	dest_pg += PG_DIR(current->start_code);
	
	/* check if there is a pg_dir at src */
	src = *(unsigned long *)src_pg;
	if (!(src & 1)) return 0;
	src &= 0xfffff000;
	src_pg = src + ((addr >> 10) & 0xffc);
	phys_addr = *(unsigned long *)src_pg;
	
	/* check if the page clean and present */
	if ((phys_addr & 0x41) != 0x01) return 0;	/* 0x40 -- D, 0x01 -- P */
	phys_addr &= 0xfffff000;
	if (phys_addr < LOW_MEM || phys_addr >= HIGH_MEM) return 0;
	
	dest = *(unsigned long *)dest_pg;
	if (!(dest & 1)) {
		if (dest = get_free_page()) {
			*(unsigned long *)dest_pg = dest | 7;
		} else {
			panic("out of memory");
		}
	}
	dest &= 0xfffff000;
	dest_pg = dest + ((addr >> 10) & 0xffc);
	if (*(unsigned long *)dest_pg & 1)
		panic("try_to_share: dest_pg already exists")
		
	/* share and write_protect */
	*(unsigned long *)src_pg &= ~2;
	*(unsigned long *)dest_pg = *(unsigned long *)src_pg;
	invalidate();
	phys_addr -= LOW_MEM;
	phys_addr >>= 12;
	mem_map[phys_addr]++;
	return 1;
}

/* find a process that can share a page with the current one */
static int share_page(unsigned long addr) {
	struct task_struct **p;
	
	if (!current->executable) return 0;
	if (current->executable->i_count < 2) return 0;
	for (p = &LAST_TASK; p > &FIRST_TASK; --p) {
		if (!*p) continue;
		if (current == *p) continue;
		if ((*p)->executable != current->executable) continue;
		if (try_to_share(addr, *p)) return 1;
	}
	return 0;
}

/* process the no-page-exception */
void do_no_page(unsigned long err_code, unsigned long addr) {
	int nr[4];
	unsigned long tmp, page;
	int block, i;
	
	addr &= 0xfffff000;
	tmp = addr - current->start_code;	/* offset in current task space */
	if (!current->executable || tmp >= current->end_data) {
		get_empty_page(addr);
		return;
	}
	if (share_page(tmp)) return;
	if (!(page = get_free_page())) panic("out of memory");
	
	block = tmp / BLOCK_SIZE + 1; /* 1 for header */
	for (i = 0; i < 4; ++block, ++i)
		nr[i] = bmap(current->executable, block);
	bread_page(page, current->executable->i_dev, nr);
	
	i = tmp + 4096 - current->end_data;
	tmp = page + 4096;
	while (i-- > 0) *(char *)(--tmp) = 0;
	if (put_page(page, addr)) return;
	free_page(page);
	panic("out of memory");
}

void mem_init(long start_mem, long end_mem) {
	int i;
	
	HIGH_MEM = end_mem;
	for (i = 0; i < PAGING_PAGE; ++i) mem_map[i] = USED;
	i = MAP_NR(start_mem);	/* start page */
	end_mem -= start_mem;
	end_mem >>= 12;	/* page nr */
	while (end_mem-- > 0) mem_map[i++] = 0;
}

void calc_mem(void) {
	int i, j, k, free = 0;
	long *pg_table;
	
	for (i = 0; i < PAGING_PAGE; ++i) 
		if (!mem_map[i]) free++;
	printk("%d pages free (of %d)\n\r", free, PAGING_PAGE);
	
	for (i = 2; i < 1024; ++i) {
		if (pg_dir[i] & 1) {
			pg_table = (long *)(0xfffff000 & pg_dir[i]);
			for (j = k = 0; j < 1024; ++j) if (pg_table[j] & 1) k++;
			printk("pg_dir[%d] uses %d pages\n", i, k);
		}
	}
}