
#include "grnb.h"


///////////////////////////////////////////////////////////////////////////////

extern u8 realmode_code_start;
extern u8 realmode_code_end;

extern u8 intcall_lowmem;
extern u8 realmode_jump_lowmem;
extern u8 bioscon_checkkey_lowmem;

void (*intcall)(u8 int_no, BIOSREGS *ireg, BIOSREGS *oreg);
void (*realmode_jump)(u32 addr, BIOSREGS *ireg);
int (*bioscon_checkkey)(void);


void intcall_setup(void)
{
	memcpy((void*)0x7200, &realmode_code_start, (&realmode_code_end-&realmode_code_start));

	intcall = (void*)(0x7200 + (&intcall_lowmem - &realmode_code_start));
	realmode_jump = (void*)(0x7200 + (&realmode_jump_lowmem - &realmode_code_start));
	bioscon_checkkey = (void*)(0x7200 + (&bioscon_checkkey_lowmem - &realmode_code_start));
}

void initregs(BIOSREGS *regs)
{
	memset(regs, 0, sizeof(BIOSREGS));
	regs->eflags = 0x00000002;
}

///////////////////////////////////////////////////////////////////////////////



void hexdump(char *str, u32 addr, int size)
{
	int i, j;

	if(str && *str)
		printk("%s:\n", str);

	for(i=0; i<size; i+=16){
		if(str==NULL){
			printk("%08x:", addr);
		}else{
			printk("%04x:", i);
		}

		for(j=0; j<16; j++){
			printk(" %02x", *(u8*)(addr+j));
			if(j==7)
				printk(" ");
		}

		printk("  ");
		for(j=0; j<16; j++){
			int ch = *(u8*)(addr+j);
			if(ch<0x20 || ch>0x7f)
				ch = '.';
			printk("%c", ch);
		}
		printk("\n");

		addr += 16;
	}
	printk("\n");
}

static u32 dmem_addr = 0;

int command_dmem(int argc, char *argv[])
{
	u32 addr;
	int size = 256;

	if(argc<=1){
		addr = dmem_addr;
	}else{
		if(argc>1){
			addr = strtoul(argv[1], NULL, 16, NULL);
		}
		if(argc>2)
			size = strtoul(argv[2], NULL, 16, NULL);
	}

	hexdump(NULL, addr, size);
	dmem_addr = addr + size;

	return 0;
}

DEFINE_CMD(dmem, command_dmem, "Dump Memory", "\
  dmem                dump 256 bytes from last address.\n\
  dmem {addr} [size]  dump [size] bytes from {addr}.\n\
");


int command_inp(int argc, char *argv[])
{
	int addr, data;

	addr = strtoul(argv[1], NULL, 16, NULL);
	data = inb(addr);
	printk("Port %04x: %02x\n", addr, data);

	return 0;
}

DEFINE_CMD(inp, command_inp, "I/O port read", "\
  inp {addr}          Read data from I/O port {addr}.\n\
");


int command_outp(int argc, char *argv[])
{
	int addr, data;

	addr = strtoul(argv[1], NULL, 16, NULL);
	data = strtoul(argv[2], NULL, 16, NULL);
	outb(data, addr);
	printk("Port %04x <- %02x\n", addr, data);

	return 0;
}

DEFINE_CMD(outp, command_outp, "I/O port write", "\
  outp {addr} {data}  Write {data} to I/O port {addr}.\n\
");


///////////////////////////////////////////////////////////////////////////////

char config_file_name[256];
u8  *config_file_buf;
int  config_file_length;

typedef struct _title_entry_t_ {
	char *name;
	int total_cmds;
	char *cmds[1];
}TITLE_ENTRY;

TITLE_ENTRY *title_list[16];
int total_titles = 0;

int config_default = 0;
int config_timeout = 5;
int config_console = VGA_CON;

MENU_DESC main_menu;


int get_default(void)
{
	u8 *buf = config_file_buf + config_file_length;

	if(buf[0]==0x5a && buf[1]==0xa5)
		return buf[2];

	return -1;
}

void save_default(int val)
{
	u8 *buf = config_file_buf + config_file_length;

	buf[0] = 0x5a;
	buf[1] = 0xa5;
	buf[2] = val;
	save_file(config_file_name, config_file_buf);
}


int menu_title_handle(int index, int act)
{
	TITLE_ENTRY *title;
	int i, retv;

	title = title_list[index];

	if(act==0x0d || act==0x0a){
		save_default(index);
		console_clear();
		console_setcursor(1);

		if(strncmp(title->cmds[0], "shell", 5)==0){
			/* exit menu and run shell */
			return 99;
		}
		for(i=0; i<title->total_cmds; i++){
			retv = exec_cmdstr(title->cmds[i]);
			if(retv){
				printk("command failed: %d  [%s]\n", retv, title->cmds[i]);
				return -1;
			}
		}
	}

	return 0;
}


char *skip_space(char *str)
{
	while(*str==' ' || *str=='\t') str++;
	return str;
}

int parse_config(char *name)
{
	u8 *buf = (u8*)DEFAULT_LOAD_ADDRESS;
	char *param;
	char *lines[128];
	int len, i, p, n, t, is_title;

	main_menu.num = 0;

	len = load_file(name, buf);
	if(len<0){
		printk("Open file %s failed!\n", name);
		return -1;
	}

	strcpy(config_file_name, name);
	config_file_length = len;
	config_file_buf = (u8*)malloc(len+64);
	memcpy(config_file_buf, buf, len+64);

	p = 0;
	n = 0;
	while(p<len){
		lines[n] = (char*)buf+p;

		while(p<len &&  buf[p]!='\r' && buf[p]!='\n') p++;
		while(p<len && (buf[p]=='\r' || buf[p]=='\n')) buf[p++]= 0;

		lines[n] = skip_space(lines[n]);
		if(lines[n][0]=='#' || lines[n][0]==0)
			continue;
		n += 1;
	}
	buf[p] = 0;

	is_title = 0;
	p = 0;
	t = 0;
	while(p<n){
		if(is_title==0){
			if(strncmp(lines[p], "title", 5)==0){
				is_title = p;
			}else if(strncmp(lines[p], "default", 7)==0){
				param = skip_space(lines[p]+7);
				config_default = strtoul(param, NULL, 0, NULL);
			}else if(strncmp(lines[p], "timeout", 7)==0){
				param = skip_space(lines[p]+7);
				config_timeout = strtoul(param, NULL, 0, NULL);
			}else if(strncmp(lines[p], "console", 7)==0){
				param = skip_space(lines[p]+7);
				if(strcmp(param, "vga")==0){
					config_console = VGA_CON;
				}else if(strcmp(param, "ser")==0){
					config_console = SER_CON;
				}
			}else{
				printk("Unknow config cmd! {%s}\n", lines[p]);
			}
		}else{
			if(p==(n-1) || strncmp(lines[p], "title", 5)==0){
				len = p-is_title-1;
				if(p==(n-1))
					len += 1;

				title_list[t] = (TITLE_ENTRY*)malloc(sizeof(TITLE_ENTRY)+len*4);
				title_list[t]->total_cmds = len;

				param = skip_space(lines[is_title]+6);
				title_list[t]->name = (char*)malloc(strlen(param)+8);
				strcpy(title_list[t]->name, param);

				for(i=0; i<len; i++){
					title_list[t]->cmds[i] = (char*)malloc(strlen(lines[is_title+1+i])+8);
					strcpy(title_list[t]->cmds[i], lines[is_title+1+i]);
				}

				t += 1;
				is_title = p;
			}
		}

		p += 1;
	}

	total_titles = t;


	n = get_default();
	if(n>=0 && n<total_titles){
		config_default = n;
	}


	main_menu.num = 0;
	for(i=0; i<total_titles; i++){
		add_menu_item(&main_menu, title_list[i]->name);
	}
	main_menu.current = config_default;
	main_menu.timeout = config_timeout;
	main_menu.handle  = menu_title_handle;

	return 0;
}


///////////////////////////////////////////////////////////////////////////////

int read_rtc(void)
{
	BIOSREGS iregs, oregs;
	int h, m, s, t;

	initregs(&iregs);
	iregs.ah = 0x02;
	intcall(0x1a, &iregs, &oregs);

	h = oregs.ch;
	m = oregs.cl;
	s = oregs.dh;

	s = (s>>4)*10 + (s&0x0f);
	m = (m>>4)*10 + (m&0x0f);
	h = (h>>4)*10 + (h&0x0f);
	t = h*60*60 + m*60 + s;

	return t;
}


///////////////////////////////////////////////////////////////////////////////


int grnb_main(void)
{

	intcall_setup();

	console_init(VGA_CON);
	//console_init(SER_CON);
	console_setcursor(0);

	printk("GRNB Start!\n");

	heap_init(0xc0000);

	scan_drive();

	parse_config("/menu.txt");
	if(config_console!=VGA_CON){
		console_setcursor(1);
		printk("Switch to serial console ...\n");
		console_init(config_console);
	}

	while(1){
		if(main_menu.num){
			menu_run(&main_menu);
		}

		grnb_shell();
		printk("Shell exit!\n");
	}

	return 0;
}

