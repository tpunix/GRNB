

#include "grnb.h"


void (*console_clear)(void);
void (*console_putc)(char ch);
int  (*console_getc)(void);
int  (*console_getc_nowait)(void);
void (*console_setcursor)(int on);
void (*console_goto)(int x, int y);
int (*console_setattr)(int attr);
void (*console_sync)(void);


static int bioscon_getc(void);
static int bioscon_getc_nowait(void);
static void bioscon_goto(int x, int y);



static u8 gtable_vga[] = {218, 191, 192, 217, 196, 179,};
static u8 gtable_ser[] = {'+', '+', '+', '+', '-', '|',};
static u8 *gtable;

#define S_UL  gtable[0]
#define S_UR  gtable[1]
#define S_DL  gtable[2]
#define S_DR  gtable[3]
#define S_HL  gtable[4]
#define S_VL  gtable[5]


static int vc_w, vc_h, vc_attr;

static int scroll_count = -1;


///////////////////////////////////////////////////////////////////////////////

void reset_scroll_lock(int value)
{
	scroll_count = value;
}

///////////////////////////////////////////////////////////////////////////////


// VGA Console

static int vc_x, vc_y;
static int vc_cursor;
static u8 *vc_fb;

static void vgacon_move_cursor(int x, int y)
{
	int pos;

	if(vc_cursor==0)
		return;

	pos = y*vc_w+x;

	outb(0x0f, 0x3D4);
	outb(pos&0xff, 0x3D5);

	outb(0x0e, 0x3D4);
	outb((pos>>8), 0x3D5);
}

static void vgacon_setcursor(int on)
{
	if(on){
		outb(0x0a, 0x3D4);
		outb(0x0d, 0x3D5);
		outb(0x0b, 0x3D4);
		outb(0x0e, 0x3D5);
		vc_cursor = 1;
	}else{
		outb(0x0a, 0x3D4);
		outb(0x20, 0x3D5);
		vc_cursor = 0;
	}
}

static void vgacon_scrollup(int line)
{
	int x, y;
	u8 *src = vc_fb;
	u8 *dst = vc_fb+line*vc_w*2;

	for(y=0; y<(vc_h-line); y++){
		memcpy(src, dst, vc_w*2);
		src += vc_w*2;
		dst += vc_w*2;
	}

	for(y=(vc_h-line); y<vc_h; y++){
		dst = vc_fb+y*vc_w*2;
		for(x=0; x<vc_w; x++){
			dst[x*2+0] = 0x20;
			dst[x*2+1] = vc_attr;
		}
	}
}

static void vgacon_puts(char *str)
{
	u8 *p = vc_fb + vc_y*vc_w*2 + vc_x*2;
	int ch;

	while(1){
		ch = *str++;
		if(ch==0)
			break;
		p[0] = ch;
		p[1] = vc_attr;
		p += 2;
	}

}

static void vgacon_putc(char ch)
{
	u8 *p = vc_fb + vc_y*vc_w*2 + vc_x*2;

	if(scroll_count==(vc_h-2)){
		scroll_count = 0;
		vc_x = 0;
		vgacon_puts("<Press any key to continue ...>");
		console_getc();
		vgacon_puts("                               ");
	}

	if(ch=='\r'){
		vc_x = 0;
	}else if(ch=='\b'){
		if(vc_x)
			vc_x -= 1;
	}else if(ch=='\n'){
		vc_x = 0;
		vc_y += 1;
		if(scroll_count>=0)
			scroll_count += 1;
	}else{
		p[0] = ch;
		p[1] = vc_attr;
		vc_x += 1;
	}

	if(vc_x>=vc_w){
		vc_x = 0;
		vc_y += 1;
	}

	if(vc_y>=vc_h){
		vgacon_scrollup(1);
		vc_y -= 1;
	}

	vgacon_move_cursor(vc_x, vc_y);
}

static void vgacon_goto(int x, int y)
{
	if(x>=0 && x<vc_w){
		vc_x = x;
	}
	if(y>=0 && y<vc_h){
		vc_y = y;
	}
}

static int vgacon_setattr(int attr)
{
	int old_attr = vc_attr;

	if(attr)
		vc_attr = attr;

	return old_attr;
}

static void vgacon_clear(void)
{
	vgacon_scrollup(vc_h);
	vc_x = 0;
	vc_y = 0;
	bioscon_goto(0, 0);
}

static void vgacon_sync(void)
{
	bioscon_goto(vc_x, vc_y);
}

static int vgacon_init(void)
{
	vc_w = 80;
	vc_h = 25;
	vc_x = 0;
	vc_y = 0;
	vc_attr = 0x07;
	vc_fb = (u8*)0xb8000;
	vc_cursor = 1;

	console_putc = vgacon_putc;
	console_getc = bioscon_getc;
	console_getc_nowait = bioscon_getc_nowait;
	console_setcursor = vgacon_setcursor;
	console_setattr = vgacon_setattr;
	console_goto = vgacon_goto;
	console_clear = vgacon_clear;
	console_sync = vgacon_sync;

	gtable = gtable_vga;

	vgacon_scrollup(vc_h);

	return 0;
}


///////////////////////////////////////////////////////////////////////////////
// BIOS Console

static int bioscon_getc(void)
{
	BIOSREGS iregs, oregs;

	initregs(&iregs);

	iregs.ah = 0x00;
	intcall(0x16, &iregs, &oregs);

	return oregs.ax;
}

static int bioscon_getc_nowait(void)
{
	if(bioscon_checkkey()){
		return bioscon_getc();
	}

	return 0;
}

static void bioscon_goto(int x, int y)
{
	BIOSREGS iregs, oregs;

	initregs(&iregs);

	iregs.ah = 0x02;
	iregs.dh = y;
	iregs.dl = x;
	iregs.bh = 0;
	intcall(0x10, &iregs, &oregs);
}


///////////////////////////////////////////////////////////////////////////////
// Serial Console

static void sercon_putc(char ch)
{
	outb(ch, 0x3f8);
	while((inb(0x3fd)&0x40)==0);
}

static void sercon_puts(char *str)
{
	int ch;

	while(1){
		ch = *str++;
		if(ch==0)
			break;
		if(ch=='\n')
			sercon_putc('\r');
		sercon_putc(ch);
	}
}


static u8 esc_buf[4];
static int esc_len, esc_vlen;


static int sercon_getc_raw(void)
{
	if((inb(0x3fd)&0x01)==0)
		return -1;
	return inb(0x3f8);
}

static int esc_cnt;

static int sercon_getc_emu(int nowait)
{
	int ch;

	if(esc_vlen){
		ch = esc_buf[0];
		esc_vlen = 0;
		return ch;
	}

	do{

		do{
			ch = sercon_getc_raw();
			if(ch<0){
				bioscon_checkkey();
				if(esc_cnt){
					esc_cnt += 1;
					if(esc_cnt>2){
						esc_len = 0;
						esc_vlen = 0;
						esc_cnt = 0;
						return 0x001b;
					}
				}
				if(nowait)
					return 0;
			}
		}while(ch<0);

		if(esc_len==0){
			if(ch==0x1b){
				esc_buf[0] = 0x1b;
				esc_len = 1;
				esc_cnt = 1;
				continue;
			}
		}else if(esc_len==1){
			if(ch==0x5b){
				esc_buf[esc_len] = ch;
				esc_len += 1;
				continue;
			}else{
				esc_buf[0] = ch;
				ch = 0x1b;
				esc_len = 0;
				esc_vlen = 1;
			}
		}else if(esc_len==2){
			if(ch==0x41){
				ch = 0x4800; // UP
			}else if(ch==0x42){
				ch = 0x5000; // DOWN
			}else if(ch==0x44){
				ch = 0x4B00; // LEFT
			}else if(ch==0x43){
				ch = 0x4d00; // RIGHT
			}
			esc_len = 0;
			esc_cnt = 0;
		}
	}while(esc_len);

	return ch;
}

static int sercon_getc(void)
{
	return sercon_getc_emu(0);
}

static int sercon_getc_nowait(void)
{
	return sercon_getc_emu(1);
}

static void sercon_setcursor(int on)
{
	if(on){
		sercon_puts("\033[?25h");
	}else{
		sercon_puts("\033[?25l");
	}
}

static void sercon_goto(int x, int y)
{
	char buf[16];

	sprintf(buf, "\033[%d;%dH", y+1, x+1);
	sercon_puts(buf);
}

static int sercon_setattr(int attr)
{
	int old_attr = vc_attr;
	int bg, fg;
	char buf[16];

	if(attr==0)
		return old_attr;
	vc_attr = attr;

	bg = vc_attr>>4;
	fg = vc_attr&0x0f;
	if(fg>7){
		fg = 90+fg-7;
	}else{
		fg = 30+fg;
	}
	if(bg>7){
		bg = 100+bg-7;
	}else{
		bg = 40+bg;
	}

	sprintf(buf, "\033[%d;%dm", bg, fg);
	sercon_puts(buf);

	return old_attr;
}

static void sercon_clear(void)
{
	sercon_puts("\033[2J");
	sercon_goto(0, 0);
}

static void sercon_sync(void)
{
}

int sercon_init(void)
{
	char buf[16];
	int i, p, ch;

	vc_attr = 0x07;
	esc_vlen = 0;
	esc_len = 0;
	esc_cnt = 0;

	outb(0x83, 0x3fb);
	outb(0x01, 0x3f8);
	outb(0x00, 0x3f9);
	outb(0x03, 0x3fb);
	outb(0x87, 0x3fa);

	console_putc = sercon_putc;
	console_getc = sercon_getc;
	console_getc_nowait = sercon_getc_nowait;
	console_setcursor = sercon_setcursor;
	console_setattr = sercon_setattr;
	console_goto = sercon_goto;
	console_clear = sercon_clear;
	console_sync = sercon_sync;

	gtable = gtable_ser;

	// get terminal size
	sercon_goto(1000, 1000);
	sercon_puts("\033[6n");
	i = 0;
	p = 0;
	while(1){
		while(1){
			ch = sercon_getc_raw();
			if(ch>=0)
				break;
		}
		if(ch=='R')
			break;
		if(ch==';')
			p = i;
		buf[i] = ch;
		i += 1;
	}
	buf[p] = 0;
	buf[i] = 0;
	vc_h = strtoul(buf+2, NULL, 10, NULL);
	vc_w = strtoul(buf+p+1, NULL, 10, NULL);
	sercon_goto(0, 0);

	return 0;
}





///////////////////////////////////////////////////////////////////////////////


int console_init(int type)
{
	if(type==VGA_CON){
		return vgacon_init();
	}else{
		return sercon_init();
	}
}


///////////////////////////////////////////////////////////////////////////////
// MENU

static void draw_menu_item(int index, char *str, int select)
{
	int i, sel_attr, old_attr;
	char buf[120];

	old_attr = console_setattr(0);
	sel_attr = (old_attr>>4) | (old_attr<<4);
	sel_attr &= 0xff;

	memset(buf, 0x20, 120);
	memcpy(buf+1, str, strlen(str));

	if(select){
		console_setattr(sel_attr);
	}
	console_goto(1, 2+index);
	for(i=0; i<vc_w-2; i++){
		console_putc(buf[i]);
	}

	console_setattr(old_attr);
}


static void draw_menu_frame(MENU_DESC *menu)
{
	int i, bottom;

	//bottom = vc_h-5;
	bottom = 2 + menu->num;

	console_setcursor(0);
	console_clear();

	printk(" GRNB V0.1  Boot Menu");

	console_goto(0, 1);
	console_putc(S_UL);
	for(i=1; i<vc_w-1; i++){
		console_putc(S_HL);
	}
	console_putc(S_UR);

	for(i=2; i<bottom; i++){
		console_goto(0, i);
		console_putc(S_VL);
		console_goto(vc_w-1, i);
		console_putc(S_VL);
	}

	console_goto(0, bottom);
	console_putc(S_DL);
	for(i=1; i<vc_w-1; i++){
		console_putc(S_HL);
	}
	console_putc(S_DR);
}


void add_menu_item(MENU_DESC *menu, char *item)
{
	menu->items[menu->num] = item;
	menu->num += 1;
}

static void menu_update(MENU_DESC *menu, int timeout)
{
	int i, select;

	console_goto(40, 0);
	printk("%2d", timeout);

	for(i=0; i<menu->num; i++){
		select = (i==menu->current)? 1: 0;
		draw_menu_item(i, menu->items[i], select);
	}
}

void menu_prompt(char *msg)
{
	console_goto(0, vc_h-4);
	printk("%s\n", msg);
}

int menu_run(MENU_DESC *menu)
{
	int retv, timeout, now, last;
	int ch, ctrl;

	timeout = menu->timeout;

	draw_menu_frame(menu);
	menu_update(menu, timeout);

	last = read_rtc();
	while(1){
		ch = console_getc_nowait();
		if(ch==0){
			if(timeout==0)
				continue;
			now = read_rtc();
			if(now>last){
				timeout -= 1;
				menu_update(menu, timeout);
				if(timeout==0){
					ch = 0x0d;
				}else{
					last = now;
					continue;
				}
			}else{
				continue;
			}
		}
		timeout = 0;
		//console_goto(0, vc_h-4); printk("ch=%04x    ", ch);

		ctrl = ch>>8;
		ch &= 0xff;
		if(ctrl==0x48){
			if(menu->current>0){
				menu->current -= 1;
				menu_update(menu, timeout);
			}
		}else if(ctrl==0x50){
			if(menu->current<(menu->num-1)){
				menu->current += 1;
				menu_update(menu, timeout);
			}
		}else{
			retv = menu->handle(menu->current, ch);
			if(retv==99)
				break;
		}
	}

	return 0;
}


///////////////////////////////////////////////////////////////////////////////

