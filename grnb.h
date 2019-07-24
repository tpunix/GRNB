
#ifndef _GRNB_H_
#define _GRNB_H_


///////////////////////////////////////////////////////////////////////////////

typedef unsigned  char  u8;
typedef unsigned short  u16;
typedef unsigned   int  u32;
typedef unsigned long long u64;


#ifndef NULL
# define NULL (void*)0
#endif

// C use symbols defined in assembly or ldscript
#if !defined(__WIN32__) && !defined(__APPLE__)
# define adecl(s) _##s
#else
# define adecl(s) s
#endif

///////////////////////////////////////////////////////////////////////////////


typedef struct cmd_desc_t{
	char *name;
	int (*func)(int argc, char *argv[]);
	char *help_msg;
	char *help_long;
	struct cmd_desc_t **sub_cmd;
}CMD_DESC;


#define add_this_cmd(cmd) CMD_DESC __attribute__((unused,__section__(".cmdesc"))) *__add_##cmd = &cmd

#define DEFINE_CMD(cmd, _func, _help_msg, _help_long) \
    static CMD_DESC cmd_##cmd = { \
        .name      = #cmd, \
        .func      = _func, \
        .help_msg  = _help_msg, \
        .help_long = _help_long, \
		.sub_cmd   = NULL, \
    }; \
    add_this_cmd(cmd_##cmd);

#define DEFINE_CMDS(cmd, _help_msg, _sub_cmd) \
    static CMD_DESC cmd_##cmd = { \
        .name      = #cmd, \
        .func      = NULL, \
        .help_msg  = _help_msg, \
        .help_long = NULL, \
        .sub_cmd   = _sub_cmd, \
    }; \
    add_this_cmd(cmd_##cmd);

#define DEFINE_SUB_CMD(cmd, _func, _help_msg, _help_long) \
    static CMD_DESC cmd_##cmd = { \
        .name      = #cmd, \
        .func      = _func, \
        .help_msg  = _help_msg, \
        .help_long = _help_long, \
        .sub_cmd   = NULL, \
    };

int exec_cmd(int argc, char *argv[]);
int exec_cmdstr(char *str);
int grnb_shell(void);

///////////////////////////////////////////////////////////////////////////////


#define VGA_CON 1
#define SER_CON 2

int console_init(int type);
extern void (*console_clear)(void);
extern void (*console_putc)(char ch);
extern int  (*console_getc)(void);
extern int  (*console_getc_nowait)(void);
extern void (*console_setcursor)(int on);
extern void (*console_goto)(int x, int y);
extern int  (*console_setattr)(int attr);
extern void (*console_sync)(void);

void reset_scroll_lock(int value);

int read_rtc(void);

///////////////////////////////////////////////////////////////////////////////

typedef struct menu_desc_t {
	int num;
	int current;
	int timeout;
	char *items[18];
	int (*handle)(int index, int act);
}MENU_DESC;


void add_menu_item(MENU_DESC *menu, char *item);
int menu_run(MENU_DESC *menu);
void menu_prompt(char *msg);


///////////////////////////////////////////////////////////////////////////////


void *memset(void *s, int v, int n);
void *memcpy(void *to, void *from, int n);
char *strcpy(char *dst, char *src);
char *strncpy(char *dst, char *src, int n);
int strcmp(char *s1, char *s2);
int strncmp(char *s1, char *s2, int n);
int strcasecmp(char *s1, char *s2);
int strncasecmp(char *s1, char *s2, int n);
int strlen(char *s);
char *strchr(char *src, char ch);
unsigned int strtoul(char *str, char **endptr, int requestedbase, int *ret);

int toupper(char c);
int tolower(char c);

int printk(char *fmt, ...);
int sprintf(char *buf, char *fmt, ...);

void hexdump(char *str, u32 addr, int size);


///////////////////////////////////////////////////////////////////////////////


int heap_init(int size);
void *malloc(int size);
int free(void *p);


///////////////////////////////////////////////////////////////////////////////

struct _filesystem_t;

typedef struct _PARTITION {
	u8  index;
	u8  unuse;
	u8  flags;
	u8  type;
	u16 c_start;
	u8  h_start;
	u8  s_start;
	u16 c_end;
	u8  h_end;
	u8  s_end;
	u32 lba_start;
	u32 lba_size;
	u32 lba_base;
	struct _PARTITION *next;
	struct _filesystem_t *fs;
}PARTITION;


typedef struct _DRIVE{
	u8  drive;
	u8  flags;
	u16 sector_size;
	u64 total_sectors;
	u16 cylinders;
	u8 heads;
	u8 sectors;
	char interface_type[8];
	struct _PARTITION *parts;
	int total_parts;
}DRIVE;

extern DRIVE drive_list[16];
extern int total_drive;

extern int current_drive;
extern int current_part;


int sector_read(int drive, u64 lba, int count, void *buf);
int sector_write(int drive, u64 lba, int count, void *buf);
int scan_drive(void);
DRIVE *get_drive(int index);
PARTITION *get_partition(DRIVE *drive, int index);


///////////////////////////////////////////////////////////////////////////////

typedef struct {
	int year, mon;
	int d, h, m, s;
}TIME;

void parse_ftime(u64 ftime, TIME *t);

int unicode_to_utf8(u16 *u, int ulen, u8 *utf8);
void make_dosname(char *buf, char *name);

extern u8 lfn_utf8[256];
extern int lfn_utf8_len;

#define DEFAULT_LOAD_ADDRESS  0x04000000

typedef struct _filesystem_t
{
	char *name;

	void *(*mount)(DRIVE *drive, PARTITION *part);
	void *(*find)(void *fs_super, void *in_dir, char *name, char *next, int do_list);
	int   (*load)(void *fs_super, void *fd, u8 *buf);
	int   (*save)(void *fs_super, void *fd, u8 *buf);

	void *super;

}FSDESC;


int parse_root_str(char *str, char **end_str, int *drive, int *part);
FSDESC *check_mount(DRIVE *drive, PARTITION *part);

int load_file(char *name, u8 *buf);
int save_file(char *name, u8 *buf);






///////////////////////////////////////////////////////////////////////////////


///////////////////////////////////////////////////////////////////////////////


#include "lowio.h"

///////////////////////////////////////////////////////////////////////////////



#endif

