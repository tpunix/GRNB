
.intel_syntax
.text
.code16

///////////////////////////////////////////////////////////////
// 16bit mode                                                //
///////////////////////////////////////////////////////////////
_start:
	jmp		1f
	nop

_oem_name:
	.ascii "ConsysVX"

1:
	push	cs
	pop		ds
	mov		[_drive], dl     // store BIOS boot drive number

	mov		si, offset _str_logo
	call	puts

	cmp		byte ptr [_skip_read], 0
	jne		_read_done

	// Reads sector using BIOS Int 13h fn 42h
	push	dword ptr 0  //
	push	dword ptr [_grnb_lba]
	push	ds
	push	word ptr 0x8000 // buffer: ds:8000
	push	word ptr [_grnb_size]
	push	word ptr 16

	mov		ah, 0x42
	mov		dl, [_drive]
	mov		si, sp
	push	ss
	pop		ds
	int		0x13
	push	cs
	pop		ds
	add		sp, 16
	jnc		_read_done
	mov		si, offset _str_read_error
	call	puts
	jmp		short $

_read_done:

	// Open A20
	mov		ax, 0x2401
	int		0x15

	// Load GDT
	lgdt	_gdtr
	cli
	mov		bx, 0x10
	mov		eax, cr0
	or		al, 1
	mov		cr0, eax
	jmp		_pmode
	nop

// Messaging Print Code
puts:
1:
	lodsb
	or		al, al
	jz		2f
	mov		ah, 0x0e
	mov		bx, 7
	int		0x10
	jmp     1b
2:
	ret

// Strings
_str_logo:
	.asciz "GRNB loader start!\r\n"
_str_read_error:
	.asciz "\r\nDisk Read Error"

///////////////////////////////////////////////////////////////
// Enter 32bit mode                                          //
///////////////////////////////////////////////////////////////

_pmode:
	mov		ds, bx
	mov		es, bx
	mov		fs, bx
	mov		gs, bx
	mov		ss, bx
	jmp		0x08:offset _pmode32
	nop

	.code32
_pmode32:
	mov		esi, 0x8000
	mov		edi, ds:[0x8008]

	mov		eax, ds:[0x8000]
	cmp		eax, 0x445a4c54  // 'TLZD'
	je		_tlz_decomp

	// copy code to target
	mov		ecx, ds:[0x800c]
	shr		ecx, 2
	rep		movsd
	jmp		_done

	// decompress to target
_tlz_decomp:
	mov		esi, 0x8010
	mov		dx, 0x0100
_loop:
	test	dh, 1
	jz		1f
	lodsb
	movzx	edx, al
	bts		edx, 16
1:
	shr		edx, 1
	jnc		_match
_raw_byte:
	movsb
	jmp		_loop
_match:
	test	dh, 1
	jz		1f
	lodsb
	movzx	edx, al
	bts		edx, 16
1:
	lodsb
	shr		edx, 1
	jnc		_short_match
_long_match:
	movzx	ecx, al
	shr		ecx, 4
	add		ecx, 3
	mov		ah, al
	and		ah, 0x0f
	lodsb
	jmp		_copy
_short_match:
	cmp		al, 0xff
	je		_done
	movzx	ecx, al
	shr		ecx, 6
	add		ecx, 2
	and		ax, 0x3f
_copy:
	mov		ebx, esi
	mov		esi, edi
	movzx	eax, ax
	sub		esi, eax
	sub		esi, 1
	rep		movsb
	mov		esi, ebx
	jmp		_loop

_done:
	mov		eax, ds:[0x8008]
	jmp		eax



_gdtr:
	.word 0x0017
	.word _gdt
_gdt:
	// 0: NULL
	.word 0x0000, 0x0000
	.byte 0x00, 0x00, 0x00, 0x00
	// 1: CODE
	.word 0xffff, 0x0000
	.byte 0x00, 0x9a, 0xcf, 0x00
	// 2: DATA
	.word 0xffff, 0x0000
	.byte 0x00, 0x92, 0xcf, 0x00
_gdt_end:

///////////////////////////////////////////////////////////////
// Parameters                                                //
///////////////////////////////////////////////////////////////

.org 0x01b0

_skip_read:
	.byte 0
_drive:
	.byte 0
_grnb_size:
	.word 62
_grnb_lba:
	.long 1


///////////////////////////////////////////////////////////////
// Partition table                                           //
///////////////////////////////////////////////////////////////
.org 0x01be
_partition_table:
	.byte 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0
	.byte 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0
	.byte 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0
	.byte 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0

_mbrsign:
	.word 0xaa55



