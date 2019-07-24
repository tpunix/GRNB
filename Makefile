


TARGET = grnb.bin mbr.bin
TEXT_START = 0x03f10000

CC = gcc
LD = ld
OBJCOPY = objcopy
OBJDUMP = objdump

CFLAGS  = -Wall -Os -MMD -m32
CFLAGS += -fno-builtin -fno-asynchronous-unwind-tables -fno-ident

LDFLAGS = -no-builtin -nostartfiles
LDFLAGS += --defsym TEXT_START=$(TEXT_START) -T ld.S

###############################################################

OBJS  = start.o main.o printk.o string.o malloc.o console.o shell.o
OBJS += disk.o fs_common.o fs_fat.o boot.o fs_ntfs.o

BOBJS = $(addprefix objs/, $(OBJS))
DEPENDS = $(patsubst %.o, %.d, $(BOBJS))
EXE := $(subst _NT,.exe, $(findstring _NT, $(shell uname -s)) )

MKTLZ := $(join tools/mktlz, $(EXE))

###############################################################

all: check_output_dir $(TARGET)

grnb: $(BOBJS) ld.S
	$(LD) $(LDFLAGS) $(BOBJS) -o $@
	$(OBJDUMP) -M intel -xd $@ > dump.txt

grnb.bin: grnb $(MKTLZ)
	$(OBJCOPY) -O binary -R .comment $< $@
	tools/mktlz $@ grnb.tlz $(TEXT_START)

mbr.bin: mbr.S ld.S
	$(CC) -Wall -c mbr.S -o objs/mbr.o
	$(LD) --defsym TEXT_START=0x7c00 -T ld.S objs/mbr.o -o mbr
	$(OBJCOPY) -O binary -R .comment mbr $@

test: all
	make -C test

$(MKTLZ): tools/mktlz.c
	$(CC) -Wall -O2 -o $@ $<

clean:
	rm -f $(TARGET) $(BOBJS) grnb *.bin *.tlz mbr dump.txt
	make -C test clean

distclean:
	rm -rf $(TARGET) objs *.bin *.tlz dump.txt

###############################################################

check_output_dir:
	@for tt in $(BOBJS); \
	do \
		mkdir -p `dirname $$tt` ; \
	done


-include $(DEPENDS)


objs/%.o: %.c
	$(CC)  $(CFLAGS)   -c $< -o $@

objs/%.o: %.S
	$(CC)  -D__ASSEMBLY__ $(CFLAGS)   -c $< -o $@

###############################################################

