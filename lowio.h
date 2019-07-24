
#ifndef _LOWIO_H_
#define _LOWIO_H_

#define EFLAGS_CF  0x01
#define EFLAGS_ZF  0x40

typedef struct
{
	union {

		struct {
			u32 edi, esi, ebp, esp;
			u32 ebx, edx, ecx, eax;
			u32 fsgs, dses;
			u32 eflags;
		};
		struct {
			u16 di, hdi, si, hsi;
			u16 bp, hbp, sp, hsp;
			u16 bx, hbx, dx, hdx;
			u16 cx, hcx, ax, hax;
			u16 gs, fs, es, ds;
			u16 flags, hflags;
		};
		struct {
			u8 dil, dih, hdi2, hdi3;
			u8 sil, sih, hsi2, hsi3;
			u8 bpl, bph, hbp2, hbp3;
			u8 spl, sph, hsp2, hsp3;
			u8 bl, bh, hbx2, hbx3;
			u8 dl, dh, hdx2, hdx3;
			u8 cl, ch, hcx2, hcx3;
			u8 al, ah, hax2, hax3;
		};
	};
}BIOSREGS;

void initregs(BIOSREGS *regs);
extern void (*intcall)(u8 int_no, BIOSREGS *ireg, BIOSREGS *oreg);
extern void (*realmode_jump)(u32 addr, BIOSREGS *ireg);
extern int (*bioscon_checkkey)(void);


static inline void outb(u8 val, u16 port)
{
	asm volatile("outb %0, %1" : : "a" (val), "dN" (port));
}

static inline u8 inb(u16 port)
{
	u8 val;
	asm volatile("inb %1, %0" : "=a" (val) : "dN" (port));
	return val;
}

static inline void outw(u16 val, u16 port)
{
	asm volatile("outw %0, %1" : : "a" (val), "dN" (port));
}

static inline u16 inw(u16 port)
{
	u16 val;
	asm volatile("inw %1, %0" : "=a" (val) : "dN" (port));
	return val;
}

static inline void outd(u32 val, u16 port)
{
	asm volatile("outl %0, %1" : : "a" (val), "dN" (port));
}

static inline u32 ind(u16 port)
{
	u32 val;
	asm volatile("inl %1, %0" : "=a" (val) : "dN" (port));
	return val;
}

static inline void io_delay(void)
{
	const u16 DELAY_PORT = 0x80;
	asm volatile("outb %%al, %0" : : "dN" (DELAY_PORT));
}

#endif

