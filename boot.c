
#include "grnb.h"
#include "elf.h"


///////////////////////////////////////////////////////////////////////////////















int parse_elf(u8 *buf)
{
	Elf32_Ehdr *h  = (Elf32_Ehdr*)(buf);
	Elf32_Phdr *ph = (Elf32_Phdr*)(buf+h->phoff);
	int i;

#if 0
	printk("ELF ident:\n");
	printk("  class   : %d\n", buf[4]);
	printk("  endian  : %d\n", buf[5]);
	printk("  osabi   : %d\n", buf[7]);
	printk("ELF info:\n");
	printk("  type    : %04x\n", h->type);
	printk("  machine : %04x\n", h->machine);
	printk("  version : %08x\n", h->version);
	printk("  entry   : %08x\n", h->entry);
	printk("  phoff   : %08x\n", h->phoff);
	printk("  shoff   : %08x\n", h->shoff);
	printk("  flags   : %08x\n", h->flags);
	printk("  ehsize  : %04x\n", h->ehsize);
	printk("  phsize  : %04x\n", h->phentsize);
	printk("  phnum   : %04x\n", h->phnum);
	printk("  shsize  : %04x\n", h->shentsize);
	printk("  shnum   : %04x\n", h->shnum);
	printk("  shstr   : %04x\n", h->shstrndx);
#endif

	if(buf[4]!=1 || (h->machine!=EM_386 && h->machine!=EM_486)){
		printk("Unsupport ELF file: class=%d machine=%d\n", buf[4], h->machine);
		return -1;
	}

	printk("\n");
	for(i=0; i<h->phnum; i++){
		//printk(" %d: type: %08x offset: %08x filesz: %08x\n", i, ph->type, ph->offset, ph->filesz);
		//printk("    flag: %08x  vaddr: %08x  memsz: %08x\n", ph->flags, ph->vaddr, ph->memsz);
		//printk("   align: %08x  paddr: %08x\n", ph->align, ph->paddr);

		if(ph->type==PT_LOAD){
			printk("Load Seg %d to %08x, size %08x\n", i, ph->vaddr, ph->filesz);
			memcpy((u8*)ph->vaddr, buf+ph->offset, ph->filesz);
			if(ph->memsz > ph->filesz){
				memset((u8*)(ph->vaddr+ph->filesz), 0, (ph->memsz - ph->filesz));
			}
		}

		ph += 1;
	}

	printk("\nJump to %08x ...\n", h->entry);

	void (*go)(void) = (void*)h->entry;
	go();

	return 0;
}


static int command_kernel(int argc, char *argv[])
{
	int len;
	u8 *buf = (u8*)DEFAULT_LOAD_ADDRESS;

	len = load_file(argv[1], buf);
	if(len<0){
		printk("Load failed!\n");
		return -1;
	}

	printk("Load %s to %08x,  length %08x\n", argv[1], buf, len);

	if(*(u32*)(buf+0)==0x464c457f){
		return parse_elf(buf);
	}

	printk("Unsupport this file!\n");
	return 0;
}


DEFINE_CMD(kernel, command_kernel, "Load and boot kernel file", "");


static int command_go(int argc, char *argv[])
{
	void (*go)(void);
	int addr;

	addr = strtoul(argv[1], NULL, 16, NULL);
	go = (void*)addr;

	printk("Jump to %08x ...\n", addr);
	go();

	return 0;
}


DEFINE_CMD(go, command_go, "Jump to address", "");

///////////////////////////////////////////////////////////////////////////////


void chain_boot(void)
{
	BIOSREGS iregs;

	console_sync();

	initregs(&iregs);

	iregs.dl = 0x80;
	iregs.sp = 0x5ffc;

	realmode_jump(0x00007c00, &iregs);
}

static int command_chain(int argc, char *argv[])
{
	DRIVE *drive;
	PARTITION *part;
	int d, p, is_partition;
	char *rest;

	is_partition = 1;
	d = -1;
	p = -1;
	rest = argv[1];
	parse_root_str(argv[1], &rest, &d, &p);
	if(d>=0){
		if(p==-1){
			// (hdx)
			is_partition = 0;
			p = current_part;
		}else{
			// (hdx,y)
		}
	}else{
		// no root, use default
		d = current_drive;
		p = current_part;
	}

	drive = get_drive(d);

	if(*rest=='+'){
		// (hdx,y)+n
		int lba_addr;
		int sectors = strtoul(rest+1, &rest, 0, NULL);
		if(sectors==0 || *rest!=0){
			return -1;
		}

		if(is_partition){
			part = get_partition(drive, p);
			lba_addr = part->lba_base + part->lba_start;
		}else{
			lba_addr = 0;
		}

		sector_read(drive->drive, lba_addr, sectors, (u8*)0x7c00);
		printk("Load %d sectors from LBA %08x\n", sectors, lba_addr);

		chain_boot();
		return 0;
	}

	// chain a file
	part = get_partition(drive, p);
	FSDESC *fs = check_mount(drive, part);
	if(fs==NULL){
		return -1;
	}

	int load_length = fs->load(fs->super, rest, (u8*)0x7c00);
	if(load_length<0){
		printk("Load failed!\n");
		return -1;
	}
	printk("Load to %08x,  length %08x\n", 0x7c00, load_length);

	chain_boot();

	return 0;
}

DEFINE_CMD(chain, command_chain, "Chain boot to other bootloader", "\
  chain (hd{x})+1           chain to first sector on drive {x}.\n\
  chain (hd{x},{y})+1       chain to first sector on drive {x}, partition {y}.\n\
  chain (hd{x},{y})/{file}  chain to {file} on drive {x}, partition {y}.\n\
    if no (hdx,y), use current drive and partition.\n\
");


///////////////////////////////////////////////////////////////////////////////


