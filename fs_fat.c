
#include "grnb.h"


///////////////////////////////////////////////////////////////////////////////


#define FS_FAT12 1
#define FS_FAT16 2
#define FS_FAT32 3


typedef struct _fatdir_t {
	char name[8];
	char ext[3];
	u8 attr;
	u8 eattr;
	u8 ctime_ms;
	u16 ctime;
	u16 cdate;
	u16 adate;
	u16 cluster_high;
	u16 mtime;
	u16 mdate;
	u16 cluster;
	u32 size;
}FATDIR;


typedef struct _fatfs_t {
	DRIVE *drive;
	PARTITION *part;

	int type;
	int cluster_size;
	int root_size;

	u64 lba_base;
	u32 lba_dbr;
	u32 lba_fat1;
	u32 lba_fat2;
	u32 lba_root;
	u32 lba_data;

	FATDIR cur_dir;

	u8 *fbuf;
	int fat_sector;

	u8 *dbuf;
	int data_cluster;

}FATFS;




///////////////////////////////////////////////////////////////////////////////

static u8 lfn_unicode[256];

static void add_lfn(u8 *buf)
{
	int index;

	index = (buf[0]&0x1f) - 1;
	index *= 26;
	memcpy(lfn_unicode+index+ 0, buf+ 1, 10);
	memcpy(lfn_unicode+index+10, buf+14, 12);
	memcpy(lfn_unicode+index+22, buf+28,  4);
	if(buf[0]&0x40){
		*(u16*)(lfn_unicode+index+26) = 0;
	}

	if(index==0){
		lfn_utf8_len = unicode_to_utf8((u16*)lfn_unicode, 128, lfn_utf8);
	}
}


///////////////////////////////////////////////////////////////////////////////

static void dump_dir_info(FATDIR *dir)
{
	int y, m, d, h, mi;
	char base[12], ext[4];
	int i;

	y = (dir->mdate>>9)+1980;
	m = (dir->mdate>>5)&0x0f;
	d = (dir->mdate&0x1f);
	h = (dir->mtime>>11);
	mi= (dir->mtime>>5)&0x3f;
	printk("  %04d-%02d-%02d %02d:%02d  ", y, m, d, h, mi);

	memcpy(base, dir->name, 8);
	memcpy(ext , dir->ext , 3);

	base[8] = 0;
	for(i=7; i>=0; i--){
		if(base[i]==0x20)
			base[i] = 0;
		else
			break;
	}

	ext[3] = 0;
	if(ext[2]==0x20) ext[2] = 0;
	if(ext[1]==0x20) ext[1] = 0;
	if(ext[0]==0x20) ext[0] = 0;

	if(dir->attr&0x10){
		printk("           [");
	}else{
		printk("%9d  ", dir->size);
	}

	if(dir->ctime_ms){
		printk("%s", lfn_utf8);
	}else{
		printk("%s", base);
		if(ext[0])
			printk(".%s", ext);
	}

	if(dir->attr&0x10)
		printk("]");
	printk("\n");
}

static int read_fat(FATFS *fatfs, int sector, int length)
{
	int retv;

	if(fatfs->fat_sector != sector){
		retv = sector_read(fatfs->drive->drive, fatfs->lba_base+fatfs->lba_fat1+sector, length, fatfs->fbuf);
		if(retv)
			return retv;
		fatfs->fat_sector = sector;
	}

	return 0;
}

static u32 access_cluster(FATFS *fatfs, u32 cluster, int do_write)
{
	int retv;
	int sector, offset;
	int next_cluster;

	if(fatfs->type==FS_FAT12){
		sector = (cluster*3)>>10;
		offset = (cluster*3)&0x3ff;
		retv = read_fat(fatfs, sector, 2);
		if(retv<0)
			return 0;

		next_cluster = *(u16*)(fatfs->fbuf+(offset/2));
		if(offset&1){
			next_cluster >>= 4;
		}else{
			next_cluster &= 0x0fff;
		}
		if(next_cluster>=0x0ff8){
			next_cluster = 0;
		}

	}else if(fatfs->type==FS_FAT16){
		sector = cluster>>8;
		offset = cluster&0xff;

		retv = read_fat(fatfs, sector, 1);
		if(retv<0)
			return 0;

		next_cluster = *(u16*)(fatfs->fbuf+offset*2);
		if(next_cluster>=0xfff8){
			next_cluster = 0;
		}
	}else{
		sector = cluster>>7;
		offset = cluster&0x7f;
		retv = read_fat(fatfs, sector, 1);
		if(retv<0)
			return 0;

		next_cluster = *(u32*)(fatfs->fbuf+offset*4);
		if(next_cluster>=0x0ffffff8){
			next_cluster = 0;
		}
	}

	if(do_write || (fatfs->data_cluster != cluster)){
		int lba_addr = fatfs->cluster_size * (cluster-2);
		lba_addr += fatfs->lba_base+fatfs->lba_data;
		if(do_write){
			retv = sector_write(fatfs->drive->drive, lba_addr, fatfs->cluster_size, fatfs->dbuf);
		}else{
			retv = sector_read(fatfs->drive->drive, lba_addr, fatfs->cluster_size, fatfs->dbuf);
		}
		if(retv)
			return 0;
		fatfs->data_cluster = cluster;
	}

	return next_cluster;
}

static u32 read_cluster(FATFS *fatfs, u32 cluster)
{
	return access_cluster(fatfs, cluster, 0);
}

static u32 write_cluster(FATFS *fatfs, u32 cluster)
{
	return access_cluster(fatfs, cluster, 1);
}


static void *fat_find(void *fs_super, void *in_dir, char *name, char *next, int do_list)
{
	FATFS *fatfs = (FATFS*)fs_super;
	FATDIR *dir = (FATDIR*)in_dir;
	u32 dir_cluster;
	int i, j, retv, buf_size;
	int finish, have_lfn, list_cnt=0;
	char name83[12];

	if(do_list){
		if(dir && (dir->attr&0x10)==0){
			// 是普通文件
			dump_dir_info(dir);
			return (void*)1;
		}
	}

	if(dir==NULL){
		// find in root
		dir_cluster = 0;
		if(fatfs->type==FS_FAT32)
			dir_cluster = fatfs->lba_root;
	}else{
		dir_cluster = dir->cluster;
		if(fatfs->type!=FS_FAT12)
			dir_cluster |= (dir->cluster_high<<16);
	}

	make_dosname(name83, name);

	i = 0;
	finish = 0;
	have_lfn = 0;
	while(finish==0){
		if(dir_cluster==0){
			// FAT12/16 root record
			retv = sector_read(fatfs->drive->drive, fatfs->lba_base+fatfs->lba_root+i, 1, fatfs->dbuf);
			if(retv<0)
				return NULL;
			buf_size = 512;
			i += 1;
			if(i==fatfs->root_size)
				finish = 1;
		}else{
			dir_cluster = read_cluster(fatfs, dir_cluster);
			if(dir_cluster==0)
				finish = 1;
			buf_size = fatfs->cluster_size*512;
		}

		for(j=0; j<buf_size; j+=32){
			dir = (FATDIR*)(fatfs->dbuf+j);
			if(dir->name[0]==0 || (u8)dir->name[0]==0xe5)
				continue;
			if(dir->attr==0x08 || dir->attr==0x28)
				continue;

			if(dir->attr==0x0f){
				if(dir->name[0]&0x40){
					have_lfn = 1;
				}
				if(have_lfn){
					add_lfn(fatfs->dbuf+j);
				}
				continue;
			}

			if(have_lfn)
				dir->ctime_ms = 0xff;
			else
				dir->ctime_ms = 0x00;

			if(do_list){
				list_cnt += 1;
				dump_dir_info(dir);
			}else{
				if(have_lfn && strcasecmp(name, (char*)lfn_utf8)==0){
					goto _found;
				}else if(strncmp(name83, dir->name, 11)==0) {
					goto _found;
				}
			}
			have_lfn = 0;
		}
	}

	if(do_list)
		return (void*)list_cnt;

	return NULL;

_found:

	if( (next && *next) && (dir->attr&0x10)==0){
		// 还有下一级目录, 但本级找到了一个普通文件
		return NULL;
	}

	memcpy(&fatfs->cur_dir, dir, sizeof(FATDIR));
	return &fatfs->cur_dir;
}


///////////////////////////////////////////////////////////////////////////////

static int fat_load(void *fs_super, void *fd, u8 *buf)
{
	FATFS *fatfs = (FATFS*)fs_super;
	FATDIR *dir = (FATDIR*)fd;
	int cluster, buf_len, length;

	cluster = dir->cluster;
	if(fatfs->type!=FS_FAT12)
		cluster |= (dir->cluster_high<<16);

	buf_len = fatfs->cluster_size*512;
	length = 0;

	while(cluster){
		cluster = read_cluster(fatfs, cluster);
		memcpy(buf, fatfs->dbuf, buf_len);
		buf += buf_len;
		length += buf_len;
	}

	return dir->size;
}

static int fat_save(void *fs_super, void *fd, u8 *buf)
{
	FATFS *fatfs = (FATFS*)fs_super;
	FATDIR *dir = (FATDIR*)fd;
	int cluster, buf_len, length;

	cluster = dir->cluster;
	if(fatfs->type!=FS_FAT12)
		cluster |= (dir->cluster_high<<16);

	buf_len = fatfs->cluster_size*512;
	length = 0;

	while(cluster){
		memcpy(fatfs->dbuf, buf, buf_len);
		cluster = write_cluster(fatfs, cluster);
		buf += buf_len;
		length += buf_len;
	}

	return dir->size;
}


///////////////////////////////////////////////////////////////////////////////

#if 0
static void dump_bpb(u8 *buf, int fat_type)
{
	printk("BPB info:\n");
	buf[11] = 0;
	printk("  OEM name: %s\n", buf+3);
	printk("  sector size: %d\n", *(u16*)(buf+0x0b));
	printk("  cluster size: %d\n", *(u8*)(buf+0x0d));
	printk("  reserved sectors: %d\n", *(u16*)(buf+0x0e));
	printk("  FAT numbers: %d\n", *(u8*)(buf+0x10));
	printk("  root entries: %d\n", *(u16*)(buf+0x11));
	printk("  total sectors: %d\n", *(u16*)(buf+0x13));
	printk("  Media: %02x\n", *(u8*)(buf+0x15));
	printk("  sectors per FAT: %d\n", *(u16*)(buf+0x16));
	printk("  sectors per Track: %d\n", *(u16*)(buf+0x18));
	printk("  heads per Cylinder: %d\n", *(u16*)(buf+0x1a));
	printk("  hidden sectors: %d\n", *(u32*)(buf+0x1c));
	printk("  total sectors: %d\n", *(u16*)(buf+0x20));

	if(fat_type==FS_FAT32){
		printk("FAT32 BPB2 info:");
		printk("  sectors per FAT32: %d\n", *(u32*)(buf+0x24));
		printk("  extended flags: %04x\n", *(u16*)(buf+0x28));
		printk("  FS version: %04x\n", *(u16*)(buf+0x2a));
		printk("  root cluster: %08x\n", *(u16*)(buf+0x2c));
		printk("  FSinfo sectors: %04x\n", *(u16*)(buf+0x30));
		printk("  backup boot sectors: %04x\n", *(u16*)(buf+0x32));
		printk("  drive number: %02x\n", *(u8*)(buf+0x40));
		printk("  extended boot sig: %02x\n", *(u8*)(buf+0x42));
		printk("  Volume serial: %08x\n", *(u32*)(buf+0x43));
		buf[0x52] = 0;
		printk("  Volume label: %s\n", buf+0x47);
	}else{
		printk("FAT12/16 BPB2 info:\n");
		printk("  drive number: %02x\n", *(u8*)(buf+0x24));
		printk("  extended boot sig: %02x\n", *(u8*)(buf+0x26));
		printk("  Volume serial: %08x\n", *(u32*)(buf+0x27));
		buf[0x36] = 0;
		printk("  Volume label: %s\n", buf+0x2B);
	}
}
#endif


static void *fat_mount(DRIVE *drive, PARTITION *part)
{
	FATFS *fatfs;
	u8 buf[512];
	int retv, fat_type;
	u32 dbr_lba;

	if(part->type<=15){
		/*
		 * FAT12: 01
		 * FAT16: 04 06 0e
		 * FAT32: 0b 0c
		 */
		if(((1<<(part->type))&0x5852) ==0)
			return NULL;
	}else if(part->type!=0xee){
		return NULL;
	}

	dbr_lba = part->lba_base+part->lba_start;
	retv = sector_read(drive->drive, dbr_lba, 1, buf);
	if(retv<0)
		return NULL;

	if(strncmp((char*)buf+0x36, "FAT12", 5)==0){
		fat_type = FS_FAT12;
	}else if(strncmp((char*)buf+0x36, "FAT16", 5)==0){
		fat_type = FS_FAT16;
	}else if(strncmp((char*)buf+0x52, "FAT32", 5)==0){
		fat_type = FS_FAT32;
	}else{
		//printk("Unknow FAT type!\n");
		return NULL;
	}

	//dump_bpb(buf, fat_type);
	
	fatfs = (FATFS*)malloc(sizeof(FATFS));
	memset(fatfs, 0, sizeof(FATFS));

	fatfs->drive = drive;
	fatfs->part = part;
	fatfs->type = fat_type;
	fatfs->lba_base = part->lba_base;
	fatfs->lba_dbr = part->lba_start;

	fatfs->lba_fat1 = fatfs->lba_dbr + *(u16*)(buf+0x0e);
	if(fat_type==FS_FAT32){
		fatfs->lba_fat2 = fatfs->lba_fat1 + *(u32*)(buf+0x24);
		fatfs->lba_data = fatfs->lba_fat2 + *(u32*)(buf+0x24);
		fatfs->lba_root = *(u32*)(buf+0x2c);
	}else{
		fatfs->lba_fat2 = fatfs->lba_fat1 + *(u16*)(buf+0x16);
		fatfs->lba_root = fatfs->lba_fat2 + *(u16*)(buf+0x16);
		fatfs->lba_data = fatfs->lba_root + ((*(u16*)(buf+0x11)+15)>>4);
	}

	fatfs->cluster_size = *(u8*)(buf+0x0d);
	fatfs->root_size = (*(u16*)(buf+0x11)+15)>>4;

	fatfs->fbuf = (u8*)malloc(512*2);
	fatfs->dbuf = (u8*)malloc(fatfs->cluster_size*512);

	fatfs->fat_sector = -1;
	fatfs->data_cluster = -1;

/*
	printk("lba_base: %08x\n", (u32)fatfs->lba_base);
	printk("lba_fat1: %08x\n", fatfs->lba_fat1);
	printk("lba_fat2: %08x\n", fatfs->lba_fat2);
	printk("lba_root: %08x\n", fatfs->lba_root);
	printk("lba_data: %08x\n", fatfs->lba_data);
*/

	return fatfs;
}

///////////////////////////////////////////////////////////////////////////////

FSDESC fs_fat = {
	.name  = "FATFS",
	.mount = fat_mount,
	.find  = fat_find,
	.load  = fat_load,
	.save  = fat_save,
};

