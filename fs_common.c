
#include "grnb.h"

///////////////////////////////////////////////////////////////////////////////

extern FSDESC fs_fat;
extern FSDESC fs_ntfs;

FSDESC *fs_list[] = {
	&fs_fat,
	&fs_ntfs,
	NULL,
};


///////////////////////////////////////////////////////////////////////////////

/* Number of 100 nanosecond units from 1/1/1601 to 1/1/1970 */
#define EPOCH_BIAS  116444736000000000ull

static char mdays[12] = {31, 28, 31, 30, 31, 30, 31, 31, 30, 31, 30, 31};

u32 div64(u64, u32);

void parse_ftime(u64 ftime, TIME *t)
{
	u32 d, h, m, s;
	u32 year, mon;

	if(ftime==0){
		memset(t, 0, sizeof(TIME));
		return;
	}

	ftime -= EPOCH_BIAS;
	s = div64(ftime, (10*1000*1000));

	d = s/(24*60*60); s -= d*24*60*60;
	h = s/(60*60); s -= h*60*60;
	m = s/60; s -= m*60;

	year = d/(365*4+1); d -= year*(365*4+1);
	mon = d/365; d -= mon*365;

	if(mon==3){
		if(d==0){
			mon = 2;
			d = 365;
		}else{
			d -= 1;
		}
	}
	year = 1970+year*4+mon;

	mdays[1] = (mon==2)? 29:28;
	mon = 0;
	while(d>mdays[mon]){
		d -= mdays[mon];
		mon += 1;
	}
	mon += 1;
	d += 1;

	t->year = year;
	t->mon  = mon;
	t->d = d;
	t->h = h;
	t->m = m;
	t->s = s;

}


///////////////////////////////////////////////////////////////////////////////

u8 lfn_utf8[256];
int lfn_utf8_len;


int unicode_to_utf8(u16 *u, int ulen, u8 *utf8)
{
	u16 uc;
	u8 *d = utf8;
	int len = 0;

	while(ulen){
		uc = *u++;
		ulen--;
		if(uc==0)
			break;

		if(uc<=0x007f){
			*d++ = uc;
			len += 1;
		}else if(uc<0x07ff){
			*d++ = 0xc0 | (uc>>6);
			*d++ = 0x80 | (uc&0x3f);
			len += 2;
		}else{
			*d++ = 0xe0 | (uc>>12);
			*d++ = 0x80 | ((uc>>6)&0x3f);
			*d++ = 0x80 | (uc&0x3f);
			len += 3;
		}
	}

	utf8[len] = 0;
	return len;
}

void make_dosname(char *buf, char *name)
{
	char ch;
	int i, d, len, mlen, elen;

	memset(buf, 0x20, 11);
	len = strlen(name);

	d = 0;
	mlen = 0;
	elen = 0;
	for(i=0; i<len; i++){
		ch = name[i];
		if(ch=='.'){
			d = 8;
			continue;
		}

		if(d<8){
			mlen += 1;
		}else{
			elen += 1;
		}
		if((d>=8 && mlen==0) || mlen>8 || elen>3){
			memset(buf, 0x20, 11);
			return;
		}

		if(ch>='a' && ch<='z')
			ch = ch-'a'+'A';
		buf[d] = ch;
		d += 1;
	}

}


///////////////////////////////////////////////////////////////////////////////

/* (hd0), (hd0,0) */
int parse_root_str(char *str, char **end_str, int *drive, int *part)
{
	int d, p;
	char *rest;

	if(*str!='('){
		return -1;
	}
	str += 1;

	if(str[0]!='h' || str[1]!='d'){
		return -1;
	}
	str += 2;

	d = strtoul(str, &rest, 0, 0);
	p = -1;
	if(*rest==','){
		str = rest+1;
		p = strtoul(str, &rest, 0, 0);
	}

	if(*rest!=')'){
		return -1;
	}

	if(end_str){
		*end_str = rest+1;
	}

	*drive = d;
	if(p>=0)
		*part = p;

	return 0;
}


FSDESC *check_mount(DRIVE *drive, PARTITION *part)
{
	FSDESC *fs, *newfs;
	void *fs_super;
	int i;

	if(part->fs)
		return part->fs;

	i = 0;
	fs_super = NULL;
	while(1){
		fs = fs_list[i];
		if(fs==NULL)
			break;

		fs_super = fs->mount(drive, part);
		if(fs_super)
			break;

		i += 1;
	}
	if(fs_super==NULL)
		return NULL;

	newfs = (FSDESC*)malloc(sizeof(FSDESC));
	memcpy(newfs, fs, sizeof(FSDESC));
	newfs->super = fs_super;
	part->fs = newfs;

	return newfs;
}

FSDESC *get_fs(char *name, char **rest)
{
	int d, p;
	DRIVE *drive;
	PARTITION *part;
	FSDESC *fs;

	d = -1;
	p = -1;
	*rest = name;
	parse_root_str(name, rest, &d, &p);
	if(d>=0){
		if(p==-1){
			p = current_part;
		}
	}else{
		d = current_drive;
		p = current_part;
	}

	drive = get_drive(d);
	part = get_partition(drive, p);

	fs = check_mount(drive, part);
	if(fs==NULL){
		printk("Unknow filesystem!\n");
		return NULL;
	}

	return fs;
}

static void *find_file(FSDESC *fs, char *file, int do_list)
{
	char tbuf[128], *p, *n;
	void *dir, *cnt;

	if(file==NULL || (do_list==0 && file[0]==0))
		return NULL;

	strcpy(tbuf, file);
	p = tbuf;
	if(*p=='/')
		p++;

	if(do_list && *p==0){
		// list root
		cnt = fs->find(fs->super, NULL, NULL, NULL, 1);
		printk("        Total %4d Files\n", (int)cnt);
		return (void*)1;
	}

	dir = NULL;
	while(1){
		n = strchr(p, '/');
		if(n){
			*n = 0;
			n += 1;
		}

		dir = fs->find(fs->super, dir, p, n, 0);
		if(dir==NULL)
			return NULL;

		if(n==NULL || *n==0)
			break;
		p = n;
	}

	if(do_list){
		cnt = fs->find(fs->super, dir, NULL, NULL, 1);
		printk("        Total %4d Files\n", (int)cnt);
	}

	return dir;
}

int load_file(char *name, u8 *buf)
{
	FSDESC *fs;
	void *fd;

	fs = get_fs(name, &name);
	if(fs==NULL){
		return -1;
	}

	fd = find_file(fs, name, 0);
	if(fd==NULL)
		return -2;

	return fs->load(fs->super, fd, buf);
}

int save_file(char *name, u8 *buf)
{
	FSDESC *fs;
	void *fd;

	fs = get_fs(name, &name);
	if(fs==NULL){
		return -1;
	}

	fd = find_file(fs, name, 0);
	if(fd==NULL)
		return -2;

	return fs->save(fs->super, fd, buf);
}


///////////////////////////////////////////////////////////////////////////////

static int command_find(int argc, char *argv[])
{
	DRIVE *drive;
	PARTITION *part;
	int d;
	FSDESC *fs;
	void *fd;


	for(d=0; d<total_drive; d++){
		drive = get_drive(d);

		part = drive->parts;
		while(part){
			fs = check_mount(drive, part);
			if(fs==NULL)
				goto _next_part;

			fd = find_file(fs, argv[1], 0);
			if(fd){
				printk("Found: (hd%d,%d)/%s\n", d, part->index, argv[1]);
				current_drive = d;
				current_part = part->index;
				return 0;
			}

_next_part:
			part = part->next;
		}
	}

	printk("File not found!\n");
	return -1;
}

DEFINE_CMD(find, command_find, "Find file from all partitions", NULL);


static int command_ls(int argc, char *argv[])
{
	char *name;
	FSDESC *fs;
	void *fd;

	name = (argc>1)? argv[1] : "/";
	fs = get_fs(name, &name);
	if(fs==NULL){
		return -1;
	}

	fd = find_file(fs, name, 1);
	if(fd==NULL){
		printk("File not found!\n");
		return -1;
	}

	return 0;
}

DEFINE_CMD(ls, command_ls, "List file or directory", NULL);

static int command_load(int argc, char *argv[])
{
	u32 load_address;
	int load_length;

	load_address = DEFAULT_LOAD_ADDRESS;
	if(argc>2){
		load_address = strtoul(argv[2], NULL, 16, NULL);
	}

	load_length = load_file(argv[1], (u8*)load_address);
	if(load_length<0){
		printk("Load failed! %d\n", load_length);
		return -1;
	}

	printk("Load to %08x,  length %08x\n", load_address, load_length);
	return 0;
}

DEFINE_CMD(load, command_load, "Load file to memory", "\
  load {file}            load {file} to default address.\n\
  load {file} {address}  load {file} to {address}.\n\
");


