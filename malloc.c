
#include "grnb.h"


extern int adecl(bss_end[]);

///////////////////////////////////////////////////////////////////////////////

typedef struct _mblk_header
{
	struct _mblk_header *next;
	struct _mblk_header *prev;
	int size;
	u32 flag;
}MBLK_HEADER;


static int heap_addr, heap_size;
static int heap_used;

int heap_init(int size)
{
	MBLK_HEADER *m;
	u32 t = (u32)adecl(bss_end);

	t = (t+0x0fff)&0xfffff000;
	heap_addr = t;
	heap_size = size;
	heap_used = 0;

	m = (MBLK_HEADER*)(heap_addr);
	m->next = NULL;
	m->prev = NULL;
	m->size = heap_size-16;
	m->flag = ((u32)m)>>4;

	heap_used += 16;

	return 0;
}


void *malloc(int size)
{
	MBLK_HEADER *m = (MBLK_HEADER*)(heap_addr);
	MBLK_HEADER *n;
	u32 align_size = (size+15)&~15;

	while(m){
		if(m->flag&0x80000000)
			goto _next;
		if(m->size<align_size)
			goto _next;

		if((m->size - align_size)>=32){
			n = (MBLK_HEADER*)((u8*)m+16+align_size);
			n->size = m->size - align_size - 16;
			n->flag = ((u32)n)>>4;
			n->next = m->next;
			n->prev = m;
			m->next = n;
			if(n->next)
				n->next->prev = n;
			heap_used += 16;
		}
		m->size = align_size;
		m->flag |= 0x80000000;

		heap_used += align_size;
		return (u8*)m + 16;

_next:
		m = m->next;
	}

	return 0;
}

int free(void *memptr)
{
	MBLK_HEADER *m, *n, *p;

	if(memptr==NULL)
		return -1;

	m = (MBLK_HEADER*)((u8*)memptr-16);
	if((m->flag<<4) != (u32)m){
		printk("free: invalid memory block! %08x\n", memptr);
		return -2;
	}

	m->flag &= 0x7fffffff;
	heap_used -= m->size;

	n = m->next;
	if(n && (n->flag&0x80000000)==0){
		m->size += 16+n->size;
		m->next = n->next;
		if(n->next){
			n->next->prev = m;
		}
		n->flag = 0;
		heap_used -= 16;
	}

	p = m->prev;
	if(p && (p->flag&0x80000000)==0){
		p->size += 16+m->size;
		p->next = m->next;
		if(m->next){
			m->next->prev = p;
		}
		m->flag = 0;
		heap_used -= 16;
	}

	return 0;
}

#if 0
void heap_dump(void)
{
	MBLK_HEADER *m = (MBLK_HEADER*)(heap_addr);

	printk("HEAP at %08x: size=%08x used=%08x\n", heap_addr, heap_size, heap_used);
	while(m){
		printf("  MBLK %08x: flag=%08x size=%08x prev=%08x next=%08x\n", (u32)client_ptr(m)+16,
				m->flag, m->size, (u32)m->prev, (u32)m->next);
		m = m->next;
	}
}
#endif

///////////////////////////////////////////////////////////////////////////////

