
SECTIONS
{
	/*
	 * check to see if we defined section starts in the makefile - if not,
	 * define them here.
	 *
	 * Align everything to a 16-byte boundary if you're specifying the
	 * addresses here.
	 */

	.text TEXT_START : AT (TEXT_START)
	{
		_text_start = .;
		*(.text)
		*(.text.*)
		_text_end = .;
	}

	. = ALIGN(16);
	.rodata :
	{
		*(.rodata)
		*(.rodata.*)
		*(.rdata)
		*(.rdata.*)
		_cmd_start = .;
		*(.cmdesc)
		_cmd_end = .;
	}

	. = ALIGN(16);
	.data :
	{
		_data_start = .;
		*(.data)
		_data_end = .;
	}
	_end = .;

	. = ALIGN(16);
	.bss :
	{
		_bss_start = .;
		*(.bss)
		*(COMMON)
		_bss_end = . ;
	}

	/DISCARD/ :
	{
		*(.drectve)
		*(.note.GNU-stack)
		*(.gnu.lto_*)
	}
}

