
#if defined(__WIN32__) || defined(__APPLE__)
# define cdecl(s) _##s
#else
# define cdecl(s) s
#endif

.intel_syntax noprefix
.text
.code32

///////////////////////////////////////////////////////////////////////////////

	.global cdecl(grnb_main)

	.global _start
_start:
	jmp		1f
	nop
	nop

	.long _end-_start
	.long _start
	.long _end-_start

1:
	// reload GDT
	cli
	lgdt	_gdtr
	// setup stack
	mov		eax, offset _start-4
	mov		esp, eax

	// clear bss
	xor		eax, eax
	mov		edi, offset _bss_start
	mov		ecx, offset _bss_end
	sub		ecx, edi
	shr		ecx, 2
	rep		stosd

	jmp 	cdecl(grnb_main)


	/* u32 div64(u64, u32) */
	.global cdecl(div64)
cdecl(div64):
	push	ebp
	mov		ebp, esp

	mov		eax, [ebp+8]
	mov		edx, [ebp+12]
	div		dword ptr[ebp+16]

	mov		esp, ebp
	pop		ebp
	ret



_gdtr:
	.word 0x0027
	.long _gdt
_gdt:
	// 00: NULL
	.word 0x0000, 0x0000
	.byte 0x00, 0x00, 0x00, 0x00
	// 08: CODE
	.word 0xffff, 0x0000
	.byte 0x00, 0x9a, 0xcf, 0x00
	// 10: DATA
	.word 0xffff, 0x0000
	.byte 0x00, 0x92, 0xcf, 0x00
	// 18: CODE16
	.word 0xffff, 0x0000
	.byte 0x00, 0x9e, 0x00, 0x00
	// 20: DATA16
	.word 0xffff, 0x0000
	.byte 0x00, 0x92, 0x00, 0x00
_gdt_end:


///////////////////////////////////////////////////////////////////////////////

#define SYMS(x) ((x) - cdecl(realmode_code_start) + 0x7200)


	/* this func should be copyed to 0x7200 */
	.global cdecl(realmode_code_start)
cdecl(realmode_code_start):


///////////////////////////////////////////////////////////////////////////////


	/* void intcall(u8 int_no, BIOSREGS *ireg, BIOSREGS *oreg) */
	.global cdecl(intcall_lowmem)
cdecl(intcall_lowmem):
	push	ebp
	mov		ebp, esp
	mov		eax, [ebp+8]
	cmp		eax, [SYMS(__int00)+1]
	je		1f
	mov		[SYMS(__int00)+1], al
	jmp 1f
1:

	// use new stack at low memory
	mov		eax, 0x5ffc
	mov		esp, eax

	// save all regs
	pushad
	pushfd
	// save oreg
	mov		eax, [ebp+16]
	push	eax
	// copy iregs to stack
	sub		esp, 44
	mov		esi, [ebp+12]
	mov		edi, esp
	mov		ecx, 11
	rep
	movsd

	call	__to_realmode
	.code16

	xor		ax, ax
	mov		ss, ax
	popad
	pop		gs
	pop		fs
	pop		es
	pop		ds
	popfd

	// int xx
__int00:
	int		0x00

	// save oregs
	pushfd
	push	ds
	push	es
	push	fs
	push	gs
	pushad

	// back to protect mode
	call	__to_protmode
	.code32

	cld
	mov		edi, [esp+44]
	mov		esi, esp
	mov		ecx, 11
	rep
	movsd

	add		esp, 44+4
	popfd
	popad

	mov		esp, ebp
	pop		ebp
	ret


///////////////////////////////////////////////////////////////////////////////

	/* void realmode_jump(u32 addr, BIOSREGS *ireg) */
	.global cdecl(realmode_jump_lowmem)
cdecl(realmode_jump_lowmem):
	push	ebp
	mov		ebp, esp

	// use new stack at low memory
	mov		eax, 0x5ffc
	mov		esp, eax

	// save oreg
	push	dword ptr [ebp+8]
	// copy iregs to stack
	sub		esp, 44
	mov		esi, [ebp+12]
	mov		edi, esp
	mov		ecx, 11
	rep
	movsd

	call	__to_realmode
	.code16

	xor		ax, ax
	mov		ss, ax
	popad
	pop		gs
	pop		fs
	pop		es
	pop		ds
	popfd

	retf
	.code32


///////////////////////////////////////////////////////////////////////////////


	/* int bioscon_checkkey(void) */
	.global cdecl(bioscon_checkkey_lowmem)
cdecl(bioscon_checkkey_lowmem):
	push	ebp
	mov		ebp, esp

	// use new stack at low memory
	mov		eax, 0x5ffc
	mov		esp, eax

	call	__to_realmode
	.code16

	sti
	xor		ax, ax
	mov		es, ax
1:
	mov		eax, es:[0x046c]
	hlt
	cmp		eax, es:[0x046c]
	jz		1b

	mov		ah, 0x11
	int		0x16
	setnz	dl

	cli
	call	__to_protmode
	.code32

	movzx	eax, dl
	mov		esp, ebp
	pop		ebp
	ret


///////////////////////////////////////////////////////////////////////////////


__to_realmode:
	// put cs to cx
	shl		word ptr [esp+2], 12
	mov		cx, [esp+2]
	// save ss
	mov		eax, esp
	shr		eax, 16
	shl		eax, 12
	push	ax

	// enter 16bit protect mode
	call	1f
1:
	pop		eax
	add		eax, 0x08
	push	0x18
	push	eax
	retf
	// enter real mode
	.code16
	push	cx
	add		ax, 0x1b
	push	ax
	mov		ax, 0x20
	mov		ds, ax
	mov		es, ax
	mov		fs, ax
	mov		gs, ax
	mov		ss, ax
	mov		eax, cr0
	and		al, 0xfe
	mov		cr0, eax
	retf
	// in real mode
	pop		ss
	and		esp, 0xffff
	retf


////////////////


	.code16
__to_protmode:
	mov		ax, cs
	movzx	ecx, ax
	shl		ecx, 4
	// convert return address to 32bit
	pop		ax
	movzx	eax, ax
	add		eax, ecx
	push	eax
	// convert sp to 32bit
	movzx	eax, sp
	add		eax, ecx
	push	eax

	// get code32 address
	call	1f 
1:
	pop		ax
	add		ax, (2f-1b)
	movzx	eax, ax
	add		eax, ecx
	push	dword ptr 0x08
	push	eax
	// back to protect mode
	mov		eax, cr0
	or		al, 0x01
	mov		cr0, eax
	.byte	0x66
	retf
2:
	.code32
	pop		eax
	mov		esp, eax
	mov		ax, 0x10
	mov		ds, ax
	mov		es, ax
	mov		fs, ax
	mov		gs, ax
	mov		ss, ax
	ret


///////////////////////////////////////////////////////////////////////////////




	.global cdecl(realmode_code_end)
cdecl(realmode_code_end):

///////////////////////////////////////////////////////////////////////////////


