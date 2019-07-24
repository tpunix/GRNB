
#include "grnb.h"

///////////////////////////////////////////////////////////////////////////////

typedef struct disk_address_packet
{
	u8 size;      // 数据包尺寸(16字节)
	u8 unuse;     // ==0
	u16 count;    // 要传输的数据块个数(以扇区为单位)
	u16 offset;   // 传输缓冲地址(segment:offset)
	u16 segment;
	u64 lba;      // LBA地址
}__attribute__((__packed__)) DAP;

typedef struct drive_param_packet
{
	u16 size;          // 数据包尺寸 (26 字节)
	u16 flags;         // 信息标志
	u32 cylinders;     // 磁盘柱面数
	u32 heads;         // 磁盘磁头数
	u32 sectors;       // 每磁道扇区数
	u64 total_sectors; // 磁盘总扇区数
	u16 sector_size;   // 扇区尺寸 (以字节为单位)
	u16 dpte_seg;      // 指向DPTE的地址
	u16 dpte_offset;
	u16 dpi_key;
	u32 dpi_size;
	char bus_type[4];
	char interface_type[8];
	u8 interface_path[8];
	u8 device_path[8];
}__attribute__((__packed__)) DPP;




DRIVE drive_list[16];
int total_drive;

int current_drive;
int current_part;




///////////////////////////////////////////////////////////////////////////////

static int sector_rw(int drive, u64 lba, int count, void *buf, int do_write)
{
	BIOSREGS iregs, oregs;
	DAP *dap = (DAP*)0x7000;
	int tcnt;

	while(count){
		tcnt = (count>=8)? 8: count;
		//printk("sector_read: drive %02x lba %08x count %d\n", drive, (u32)lba, tcnt);

		dap->size = 16;
		dap->count = tcnt;
		dap->offset = 0x6000;
		dap->segment = 0;
		dap->lba = lba;

		initregs(&iregs);
		iregs.ah = 0x42;
		iregs.dl = drive;
		iregs.ds = 0;
		iregs.si = 0x7000;

		if(do_write){
			iregs.ah = 0x43;
			memcpy((void*)0x6000, buf, tcnt*512);
		}
		intcall(0x13, &iregs, &oregs);
		if(oregs.eflags&EFLAGS_CF){
			printk("Disk access failed! LBA=%08x AH=%02x\n", lba, oregs.ah);
			return -1;
		}

		if(!do_write){
			memcpy(buf, (void*)0x6000, tcnt*512);
		}
		buf += tcnt*512;
		count -= tcnt;
		lba += tcnt;

	}

	return 0;
}

int sector_read(int drive, u64 lba, int count, void *buf)
{
	return sector_rw(drive, lba, count, buf, 0);
}

int sector_write(int drive, u64 lba, int count, void *buf)
{
	return sector_rw(drive, lba, count, buf, 1);
}

static int get_drive_info(int drive, DPP *rdpp)
{
	BIOSREGS iregs, oregs;
	DPP *dpp = (DPP*)0x7000;

	dpp->size = 128;

	initregs(&iregs);
	iregs.ah = 0x48;
	iregs.dl = drive;
	iregs.si = 0x7000;
	intcall(0x13, &iregs, &oregs);
	if(oregs.eflags&EFLAGS_CF){
		return -1;
	}

	memcpy(rdpp, dpp, sizeof(DPP));
	return 0;
}


///////////////////////////////////////////////////////////////////////////////

static int scan_partition_gpt(DRIVE *drive)
{
	u8 buf[512];
	u32 pt_lba;
	int i, j, retv, index, pt_num;
	PARTITION *new, **last;

	printk("scan_partition_gpt ...\n");

	retv = sector_read(drive->drive, 1, 1, buf);
	if(retv)
		return retv;
	if(strncmp((char*)buf, "EFI PART", 8)){
		return -2;
	}

	pt_lba = *(u32*)(buf+0x48);
	pt_num = *(u32*)(buf+0x50);

	index = 0;
	last = &drive->parts;

	for(i=0; i<pt_num; i+=4){
		retv = sector_read(drive->drive, pt_lba, 1, buf);
		if(retv)
			return retv;

		for(j=0; j<4; j++){
			u8 *pbuf = buf+j*0x80;

			if(*(u32*)(pbuf+0x20)==0)
				goto _finish;

			new = (PARTITION*)malloc(sizeof(*new));
			memset(new, 0, sizeof(PARTITION));
			new->index = index;
			new->type = 0xee;
			new->lba_start = *(u32*)(pbuf+0x20);
			new->lba_size  = *(u32*)(pbuf+0x28) - new->lba_start + 1;
			new->fs = NULL;

			index += 1;
			new->next = NULL;
			*last = new;
			last = &new->next;

		}

		pt_lba += 1;
	}

_finish:

	drive->total_parts = index;
	return 0;
}

///////////////////////////////////////////////////////////////////////////////


static void dump_drive(DRIVE *d)
{
	printk("Drive %02x:  Type %s  flag:%02x", d->drive, d->interface_type, d->flags);
	printk("  CHS:%d/%d/%d  LBA:%d", d->cylinders, d->heads, d->sectors, (u32)d->total_sectors);
	if(d->flags&4)
		printk("  removable");
	printk("\n");
}


static void ptdata_to_part(PARTITION *part, u8 *p)
{
	part->flags = p[0];
	part->type = p[4];
	part->lba_start = *(u32*)(p+8);
	part->lba_size = *(u32*)(p+12);
	part->h_start = p[1];
	part->s_start = p[2]&0x3f;
	part->c_start = ((p[2]<<2)&0x0300) | p[3];
	part->h_end = p[5];
	part->s_end = p[6]&0x3f;
	part->c_end = ((p[6]<<2)&0x0300) | p[7];
}

static int part_isvalid(DRIVE *drive, u8 *pbuf, u32 lba_base)
{
	u32 *p = (u32*)pbuf;
	u32 lba_start, lba_end;

	if(p[0]==0 || p[1]==0 || p[2]==0 || p[3]==0)
		return 0;

	lba_start = p[2] + lba_base;
	lba_end   = p[3] + lba_start;

	if(lba_start >= drive->total_sectors)
		return 0;
	if(lba_end >= drive->total_sectors)
		return 0;

	return 1;
}

static void dump_one_part(PARTITION *part)
{
	printk(" %2d: %02x %02x  [%5d/%3d/%2d]  [%5d/%3d/%2d]  %08x %08x\n",
			part->index, part->flags, part->type,
			part->c_start, part->h_start, part->s_start,
			part->c_end,   part->h_end,   part->s_end,
			part->lba_start, part->lba_size
	);
}

static void dump_parts(DRIVE *drive)
{
	PARTITION *part;

	part = drive->parts;
	while(part){
		if(part->type)
			dump_one_part(part);
		part = part->next;
	}
}

static int scan_partition(DRIVE *drive)
{
	u8 buf[512];
	u32 lba;
	int i, retv, index;
	PARTITION part, *new, **last, *mp[4];

	retv = sector_read(drive->drive, 0, 1, buf);
	if(retv)
		return retv;
	if(buf[510]!=0x55 || buf[511]!=0xaa){
		return -1;
	}

	if(*(u8*)(buf+446+4)==0xee){
		return scan_partition_gpt(drive);
	}


	index = 0;
	last = &drive->parts;

	for(i=0; i<4; i++){
		ptdata_to_part(&part, buf+446+i*16);
		part.lba_base = 0;

		new = (PARTITION*)malloc(sizeof(*new));
		memcpy(new, &part, sizeof(part));
		new->index = index;
		new->fs = NULL;
		index += 1;

		//dump_one_part(new);

		new->next = NULL;
		*last = new;
		last = &new->next;

		mp[i] = new;
	}

	for(i=0; i<4; i++){
		if(mp[i]->type!=0x05 && mp[i]->type!=0x0f)
			continue;

		lba = mp[i]->lba_start;
		while(1){
			//printk("Extend PART at %d ...\n", lba);
			retv = sector_read(drive->drive, lba, 1, buf);
			if(retv)
				break;
			if(buf[510]!=0x55 || buf[511]!=0xaa){
				break;;
			}

			ptdata_to_part(&part, buf+446+0*16);
			if(!part_isvalid(drive, buf+446+0*16, lba)){
				break;
			}
			part.lba_base = lba;

			new = (PARTITION*)malloc(sizeof(*new));
			memcpy(new, &part, sizeof(part));
			new->index = index;
			new->fs = NULL;
			index += 1;

			//dump_one_part(new);
	
			new->next = NULL;
			*last = new;
			last = &new->next;

			ptdata_to_part(&part, buf+446+1*16);
			if(!part_isvalid(drive, buf+446+1*16, mp[i]->lba_start)){
				break;
			}
			//part.index = 99; dump_one_part(&part);

			lba = mp[i]->lba_start + part.lba_start;
		}

	}

	drive->total_parts = index;
	return 0;
}




int scan_drive(void)
{
	int drv, retv;
	DPP dpp;
	DRIVE *drive;

	total_drive = 0;

	for(drv=0x80; drv<0x90; drv++){
		retv = get_drive_info(drv, &dpp);
		if(retv){
			break;
		}
		dpp.bus_type[3] = 0;
		dpp.interface_type[7] = 0;

		drive = &drive_list[total_drive];
		drive->drive = drv;
		drive->flags = dpp.flags;
		drive->sector_size = dpp.sector_size;
		drive->total_sectors = dpp.total_sectors;
		drive->cylinders = dpp.cylinders;
		drive->heads = dpp.heads;
		drive->sectors = dpp.sectors;
		drive->parts = NULL;
		memcpy(drive->interface_type, dpp.interface_type, 8);

		total_drive += 1;

		dump_drive(drive);

		scan_partition(drive);
	}

	return 0;
}

DRIVE *get_drive(int index)
{
	return &drive_list[index];
}


PARTITION *get_partition(DRIVE *drive, int index)
{
	PARTITION *part;

	part = drive->parts;
	while(part){
		if(part->index == index)
			return part;
		part = part->next;
	}

	return NULL;
}



///////////////////////////////////////////////////////////////////////////////

static int command_root(int argc, char *argv[])
{
	int retv, d, p;

	if(argc>1){
		d = -1;
		p = -1;
		retv = parse_root_str(argv[1], NULL, &d, &p);
		if(retv){
			printk("format error!\n");
			return -1;
		}
		if(p==-1)
			p = 0;
		current_drive = d;
		current_part  = p;
	}

	printk("Current device: (hd%d,%d)\n", current_drive, current_part);

	return 0;
}

DEFINE_CMD(root, command_root, "Set Current Drive and Partition", "\
  root (hd{x})    set root to drive {x}, partition 0.\n\
  root (hd{x},{y})  set root to drive {x}, partition {y}.\n\
");


static int command_part(int argc, char *argv[])
{
	int i;

	for(i=0; i<total_drive; i++){
		dump_drive(&drive_list[i]);
		printk("--------------------------------\n");
		dump_parts(&drive_list[i]);
		printk("\n");
	}

	return 0;
}

DEFINE_CMD(part, command_part, "Show Partition Infomations", NULL);


static int ddisk_lba = 0;

static int command_ddisk(int argc, char *argv[])
{
	int retv, d, lba;
	DRIVE *drive;
	u8 buf[512];

	if(argc<2){
		drive = get_drive(current_drive);
		lba = ddisk_lba;
	}else if(argc<3){
		drive = get_drive(current_drive);
		lba   = strtoul(argv[1], NULL, 16, NULL);
	}else{
		d   = strtoul(argv[1], NULL, 16, NULL);
		lba = strtoul(argv[2], NULL, 16, NULL);
		drive = get_drive(d);
	}

	retv = sector_read(drive->drive, lba, 1, buf);
	if(retv)
		return retv;

	hexdump("", (u32)buf, 512);
	ddisk_lba = lba+1;

	return 0;
}

DEFINE_CMD(ddisk, command_ddisk, "Dump Disk Sector", "\
  ddisk                dump from last lba on current drive\n\
  ddisk {lba}          dump from {lba} on current drive\n\
  ddisk {drive} {lba}  dump from {lba} on {drive}\n\
");



