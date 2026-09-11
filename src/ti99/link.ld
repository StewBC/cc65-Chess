/* EA5-shaped: code in high 24K, data and BSS in low 8K.  stack at >FFFC. */

OUTPUT_FORMAT("elf32-tms9900")
OUTPUT_ARCH(tms9900)
ENTRY(_start)

SECTIONS
{
	.text 0xA000 :
	{
		*crt0.o(.text)
		*(.text)
		*(.text.*)
		*(.rodata)
		*(.rodata.*)
		*(.const)
		. = ALIGN(2);
		_etext = .;
	}

	.data 0x2000 :
	{
		_data_start = .;
		*(.data)
		*(.data.*)
		*(.sdata)
		. = ALIGN(2);
		_data_end = .;
	}

	.bss :
	{
		_bss_start = .;
		*(.bss)
		*(.bss.*)
		*(.sbss)
		*(COMMON)
		. = ALIGN(2);
		_bss_end = .;
	}

	_end = .;
}
