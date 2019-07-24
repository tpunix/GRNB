
#include "grnb.h"

///////////////////////////////////////////////////////////////////////////////

/*
 * 有些文件比较特殊，用了多个file record，并且有多个80属性分布在后面的record中。
 *
 * record n+0: attr_10 attr_20 attr_30 attr_30
 *        n+1: attr_80 (vcn: 0000-mmmm  filesize: nnnn)
 *        n+2: attr_80 (vcn: mmmm-nnnn  filesize: 0000)
 *
 * 暂时不支持读取这类文件。
 *
 */


typedef struct {
	u16 start;
	u16 next;
	u16 end;
	u16 zero;

	u32 LCN_start;
	u32 LCN;
	u32 LCN_size;
}RUNLIST;

typedef struct {
	u32 fd;
	u16 fd_high;
	u16 seq;
	u16 len;
	u16 content_len;
	u8  flag;
	u8  unuse[3];
}INDEX;

typedef struct {
	u64 parent_seq;
	u64 create_time;
	u64 modify_time;
	u64 mft_time;
	u64 access_time;
	u64 space_size;
	u64 file_size;
	u32 flags;
	u32 reparse;
	u8  name_len;
	u8  name_type;
	u8  name[2];
}NAME;


typedef struct {
	DRIVE *drive;
	PARTITION *part;

	int cluster_size;
	u64 lba_base;
	u32 lba_mft;
	u8 *mft_80;

	u8 *mbuf;
	int mbuf_index;
	u8 *attr_30;
	u8 *attr_80;
	u8 *attr_90;
	u8 *attr_a0;

	u8 *dbuf;
	int data_lcn;
}NTFS;


///////////////////////////////////////////////////////////////////////////////


static void rl_reset(RUNLIST *rl)
{
	rl->next = 0;
}

static int rl_decode_next(RUNLIST *rl)
{
	u8 *buf;
	int i, cl, sl;
	int new_start, sbyte;

	if(rl->next==0){
		rl->next = rl->start;
		rl->LCN_start = 0;
	}

	buf = (u8*)rl;
	buf += rl->next;

	if(rl->next>=rl->end || buf[0]==0)
		return -1;

	sl = buf[0]&0x0f;
	cl = buf[0]>>4;
	rl->next += sl+cl+1;

	buf += 1;
	rl->LCN_size = 0;
	for(i=0; i<sl; i++){
		rl->LCN_size |= buf[i]<<(i*8);
	}

	buf += i;
	new_start = 0;
	for(i=0; i<cl; i++){
		sbyte = (char)buf[i];
		new_start &= (1<<i*8)-1;
		new_start |= sbyte<<(i*8);
	}
	rl->LCN_start += new_start;

	rl->LCN = 0;
	return 0;
}

static u32 rl_get_lcn(RUNLIST *rl)
{
	if(rl->next==0 || rl->LCN==(rl->LCN_size-1)){
		if(rl_decode_next(rl))
			return 0;
	}else{
		rl->LCN += 1;
	}

	return rl->LCN_start + rl->LCN;
}

// give vcn, return lcn
static u32 rl_seek_vcn(RUNLIST *rl, int vcn)
{
	int vcn_start;

	rl_reset(rl);

	vcn_start = 0;
	while(1){
		if(rl_decode_next(rl))
			return 0;

		if(vcn<(vcn_start+rl->LCN_size)){
			return rl->LCN_start + ( vcn - vcn_start);
		}

		vcn_start += rl->LCN_size;
	}

}


///////////////////////////////////////////////////////////////////////////////


static void sector_fixup(u8 *buf)
{
	int p = *(u16*)(buf+4);
	int s = *(u16*)(buf+6);
	int i;

	if(*(u16*)(buf+p) == *(u16*)(buf+0x01fe)){
		for(i=0; i<(s-1); i++){
			*(u16*)(buf+i*0x0200+0x01fe) = *(u16*)(buf+p+2+i*2);
		}
	}
}

static void parse_record(NTFS *ntfs)
{
	int p;
	u8 *buf;
	RUNLIST *rl;
	NAME *name = NULL;

	sector_fixup(ntfs->mbuf);

	ntfs->attr_30 = NULL;
	ntfs->attr_80 = NULL;
	ntfs->attr_90 = NULL;
	ntfs->attr_a0 = NULL;

	p = 0x38;
	while(p<1024){
		buf = ntfs->mbuf+p;
		if(*(u32*)(buf)==0xffffffff)
			break;

		u32 id = *(u32*)(buf);
		if(id==0x30){
			u8 *dp = buf + *(u16*)(buf+0x14);

			if(dp[0x41]!=2 || ntfs->attr_30==NULL){
				ntfs->attr_30 = buf;
				name = (NAME*)(buf + *(u16*)(buf+0x14));
			}
		}else if(id==0x80){
			ntfs->attr_80 = buf;
			rl = (RUNLIST*)buf;
			if(buf[8]){
				rl->start = *(u16*)(buf+0x20);
				rl->next  = 0;

				name->space_size = *(u64*)(buf+0x28);
				name->file_size  = *(u64*)(buf+0x30);
			}else{
				name->space_size = *(u32*)(buf+0x04) - *(u16*)(buf+0x14);
				name->file_size  = *(u32*)(buf+0x10);
			}
		}else if(id==0x90){
			ntfs->attr_90 = buf;
		}else if(id==0xa0){
			ntfs->attr_a0 = buf;
			rl = (RUNLIST*)buf;
			rl->start = *(u16*)(buf+0x20);
			rl->next  = 0;
		}

		p += *(u32*)(buf+4);
	}
}

static int read_record(NTFS *ntfs, int index)
{
	RUNLIST *rl = (RUNLIST*)ntfs->mft_80;
	int retv;
	u32 lba, vcn, lcn, offset;

	if(ntfs->mbuf_index == index)
		return 0;

	vcn = (index*2) / ntfs->cluster_size;
	offset = (index*2) % ntfs->cluster_size;

	lcn = rl_seek_vcn(rl, vcn);
	if(lcn==0)
		return -1;

	lba = ntfs->lba_base + lcn*ntfs->cluster_size + offset;
	//printk("\nMFT_%08x: @%08x\n", index, lba);

	retv = sector_read(ntfs->drive->drive, lba, 2, ntfs->mbuf);
	if(retv)
		return retv;
	ntfs->mbuf_index = index;

	if(strncmp((char*)(ntfs->mbuf), "FILE", 4))
		return -1;

	parse_record(ntfs);

	return 0;
}

static int read_cluster(NTFS *ntfs, u32 lcn)
{
	u32 lba;
	int retv;

	if(ntfs->data_lcn == lcn)
		return 0;

	lba = ntfs->lba_base + lcn*ntfs->cluster_size;
	//printk("LCN %08x @ %08x\n", lcn, lba);

	retv = sector_read(ntfs->drive->drive, lba, ntfs->cluster_size, ntfs->dbuf);
	if(retv)
		return retv;

	return 0;
}


///////////////////////////////////////////////////////////////////////////////


static void dump_dir_info(NAME *n)
{
	TIME t;

	parse_ftime(n->modify_time, &t);

	printk("  %04d-%02d-%02d %02d:%02d  ", t.year, t.mon, t.d, t.h, t.m);

	if(n->flags&0x10000000){
		printk("           [");
	}else{
		printk("%9d  ", (u32)n->file_size);
	}

	unicode_to_utf8((u16*)n->name, n->name_len, lfn_utf8);
	printk("%s", lfn_utf8);

	if(n->flags&0x10000000) printk("]");

	printk("\n");
}

static u32 process_index(u8 *buf, int len, char *name, int do_list)
{
	INDEX *e;
	NAME *n;
	int p = 0;
	u32 retv = 0;

	while(p<len){
		e = (INDEX*)(buf+p);
		if(e->flag&2)
			break;

		n = (NAME*)(buf+p+0x10);
		if(e->fd>=16 && n->name_type!=2){
			if(do_list){
				dump_dir_info(n);
				retv += 1;
			}else{
				unicode_to_utf8((u16*)n->name, n->name_len, lfn_utf8);
				if(strcasecmp(name, (char*)lfn_utf8)==0){
					retv = e->fd;
					break;
				}
			}
		}

		p += e->len;
	}

	return retv;
}

static void *ntfs_find(void *fs_super, void *in_dir, char *name, char *next, int do_list)
{
	NTFS *ntfs = (NTFS*)fs_super;
	u32 FD = (u32)in_dir;
	u8 *buf;
	int retv, p, len, list_cnt=0;
	u32 lcn;

	if(FD==0)
		FD = 5; // find in root

	retv = read_record(ntfs, FD);
	if(retv<0)
		return NULL;

	if(do_list){
		if(ntfs->mbuf[0x16]!=3){
			// 是普通文件
			p = *(u16*)(ntfs->attr_30+0x14);
			dump_dir_info((NAME*)(ntfs->attr_30+p));
			return (void*)1;
		}
	}

	// parse attr_90
	buf = ntfs->attr_90;
	len = *(u32*)(buf+4);
	p = 0x40;

	FD = process_index(buf+p, len-p, name, do_list);
	if(do_list){
		list_cnt += FD;
	}else if(FD){
		goto _found;
	}


	// parse attr_a0
	if(ntfs->attr_a0==NULL)
		goto _skip_a0;

	RUNLIST *rl = (RUNLIST*)ntfs->attr_a0;
	rl_reset(rl);

	while(1){
		lcn = rl_get_lcn(rl);
		if(lcn==0)
			break;

		read_cluster(ntfs, lcn);
		buf = ntfs->dbuf;
		if(*(u32*)(buf)!=0x58444e49)
			continue;
		sector_fixup(buf);

		p = *(u32*)(buf+0x18);
		p += 0x18;

		FD = process_index(buf+p, 4096-p, name, do_list);
		if(do_list){
			list_cnt += FD;
		}else if(FD){
			goto _found;
		}
	}

_skip_a0:

	if(do_list)
		return (void*)list_cnt;

	return NULL;

_found:

	if( (next && *next) && (ntfs->mbuf[0x16]!=3)){
		// 还有下一级目录, 但本级找到了一个普通文件
		return NULL;
	}

	return (void*)FD;
}


///////////////////////////////////////////////////////////////////////////////


static int ntfs_load(void *fs_super, void *fd, u8 *buf)
{
	NTFS *ntfs = (NTFS*)fs_super;
	u32 FD = (u32)fd;
	int retv, p, size;

	retv = read_record(ntfs, FD);
	if(retv<0)
		return -3;

	if(ntfs->attr_80==NULL)
		return -4;

	if(ntfs->attr_80[8]==0){
		p = *(u16*)(ntfs->attr_80+0x14);
		size = *(u16*)(ntfs->attr_80+0x10);
		int max_size = *(u16*)(ntfs->attr_80+0x04) - p;
		memcpy(buf, ntfs->attr_80+p, max_size);
	}else{
		u32 lcn;
		int buf_len = ntfs->cluster_size*512;
		size = *(u32*)(ntfs->attr_80+0x30);

		RUNLIST *rl = (RUNLIST*)ntfs->attr_80;
		rl_reset(rl);

		while(1){
			lcn = rl_get_lcn(rl);
			if(lcn==0)
				break;

			read_cluster(ntfs, lcn);
			memcpy(buf, ntfs->dbuf, buf_len);
			buf += buf_len;
		}
	}

	return size;
}

static int ntfs_save(void *fs_super, void *fd, u8 *buf)
{
	return -1;
}


///////////////////////////////////////////////////////////////////////////////


#if 0
static void dump_bpb(u8 *buf)
{
	printk("BPB info:\n");
	buf[11] = 0;
	printk("  OEM name: %s\n", buf+3);
	printk("  sector size: %d\n", *(u16*)(buf+0x0b));
	printk("  cluster size: %d\n", *(u8*)(buf+0x0d));
	printk("  Media: %02x\n", *(u8*)(buf+0x15));
	printk("  sectors per Track: %d\n", *(u16*)(buf+0x18));
	printk("  heads per Cylinder: %d\n", *(u16*)(buf+0x1a));
	printk("  hidden sectors: %d\n", *(u32*)(buf+0x1c));

	printk("NTFS BPB2 info:\n");
	printk("  total_sectors: %08x_%08x\n", *(u32*)(buf+0x2c), *(u32*)(buf+0x28));
	printk("  MFT  start: %08x_%08x\n", *(u32*)(buf+0x34), *(u32*)(buf+0x30));
	printk("  MFT2 start: %08x_%08x\n", *(u32*)(buf+0x3c), *(u32*)(buf+0x38));
	printk("  MFT size: %08x\n", *(u32*)(buf+0x40));
	printk("  Index size: %08x\n", *(u32*)(buf+0x44));
	printk("  Logic serial: %08x_%08x\n", *(u32*)(buf+0x4c), *(u32*)(buf+0x48));
}
#endif

static void *ntfs_mount(DRIVE *drive, PARTITION *part)
{
	NTFS *ntfs;
	int retv;
	u8 buf[512];
	u32 dbr_lba;

	dbr_lba = part->lba_base+part->lba_start;
	retv = sector_read(drive->drive, dbr_lba, 1, buf);
	if(retv<0)
		return NULL;
	if(strncmp((char*)(buf+3), "NTFS", 4))
		return NULL;


	//dump_bpb(buf);

	ntfs = (NTFS*)malloc(sizeof(NTFS));
	memset(ntfs, 0, sizeof(NTFS));

	ntfs->drive = drive;
	ntfs->part = part;
	ntfs->cluster_size = *(u8*)(buf+0x0d);
	ntfs->lba_base = dbr_lba;
	ntfs->lba_mft  = *(u32*)(buf+0x30);

	ntfs->lba_mft = ntfs->lba_mft*ntfs->cluster_size + dbr_lba;

	ntfs->mbuf = (u8*)malloc(1024);
	ntfs->dbuf = (u8*)malloc(ntfs->cluster_size*512);
	ntfs->data_lcn = -1;

	// parse $MFT
	sector_read(ntfs->drive->drive, ntfs->lba_mft, 2, ntfs->mbuf);
	ntfs->mbuf_index = 0;
	parse_record(ntfs);

	int len = *(u32*)(ntfs->attr_80+4);
	ntfs->mft_80 = (u8*)malloc(len);
	memcpy(ntfs->mft_80, ntfs->attr_80, len);

	return ntfs;
}


///////////////////////////////////////////////////////////////////////////////


FSDESC fs_ntfs = {
	.name  = "NTFS",
	.mount = ntfs_mount,
	.find  = ntfs_find,
	.load  = ntfs_load,
	.save  = ntfs_save,
};


///////////////////////////////////////////////////////////////////////////////


