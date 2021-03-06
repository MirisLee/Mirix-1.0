NASM		=nasm
LD			=ld
LDFLAGS	=-s -x -M
CC			=gcc
CFLAGS	=-Wall -O -fstrength-reduce -fomit-frame-pointer -fcombine-regs
CPP		=cpp -nostdinc -Iinclude

ROOT_DEV=/dev/hd6

ARCHIEVES	=kernel/kernel.o mm/mm.o fs/fs.o
DRIVERS		=kernel/blk_dev/blk_dev.a kernel/chr_dev/chr_dev.a
LIBS			=lib/lib.a

%.o : %.c
	$(CC) $(CFLAGS) -nostdinc -Iinclude -c -o $*.o $*.c
	
%.o : %.asm
	$(NASM) -o $*.o $*.asm
	
all:	image

image:	boot/bootsect boot/setup tools/system tools/build
	tools/build boot/bootsect boot/setup tools/system > image
	sync
	
disk:		image
	dd bs=8192 if=image of=/dev/PS0
	
tools/build:	tools/build.c
	$(CC) $(CFLAGS) -o tools/build tools/build.c
	
boot/bootsect:	boot/bootsect.asm
	$(NASM) -o boot/bootsect boot/bootsect.asm
	
boot/setup:		boot/setup.asm
	$(NASM) -o boot/setup boot/setup.asm
	
boot/head.o:	boot/head.asm

tools/system:	boot/head.o init/main.o \
					$(ARCHIEVES) $(DRIVERS) $(LIBS)
	$(LD) $(LDFLAGS) boot/head.o init/main.o \
	$(ARCHIEVES) $(DRIVERS) $(LIBS) \
	-o tools/system > System.map
	
kernel/kernel.o:
	(cd kernel; make)
	
mm/mm.o:
	(cd mm; make)
	
fs/fs.o:
	(cd fs; make)
	
kernel/blk_dev/blk_dev.a:
	(cd kernel/blk_dev; make)
	
kernel/chr_dev/chr_dev.a:
	(cd kernel/chr_dev; make)
	
lib/lib.a:
	(cd lib; make)
	
clean:
	rm -f System.map tmp_make core boot/bootsect boot/setup
	rm -f init/*.o tools/system tools/build boot/*.o
	(cd kernel; make clean)
	(cd mm; make clean)
	(cd fs; make clean)
	(cd lib; make clean)
	
backup:
	clean
	(cd ..; tar cf - mirix | compress - > backup.Z)
	sync
	
dep:
	sed '/\#\#\# Dependencies/q' < Makefile > tmp_make
	(for i in init/*.c; \
		do echo -n "init/";
		$(CPP) -M $$i;
		done) >> tmp_make
	cp tmp_make Makefile
	(cd kernel; make dep)
	(cd mm; make dep)
	(cd fs; make dep)