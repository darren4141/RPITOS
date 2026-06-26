SECTIONS {
	.init 0x8000 : {
		*(.init)
	}

	.text : {
		. = ALIGN(4);
		*(.text)
		*(.text.*)
	}

	.rodata : {
		. = ALIGN(4);
		*(.rodata)
		*(.rodata.*)
	}

	.data : {
		. = ALIGN(4);
		*(.data)
	}

	.bss : {
		. = ALIGN(4);
		_bss_start = .;
		*(.bss)
		*(COMMON)
		_bss_end = .;
	}

	.stacks (NOLOAD) : {
		. = ALIGN(8);
		_fiq_stack_bottom = .;  . += 0x200;  _fiq_stack_top = .;
		_irq_stack_bottom = .;  . += 0x400;  _irq_stack_top = .;
		_abt_stack_bottom = .;  . += 0x200;  _abt_stack_top = .;
		_und_stack_bottom = .;  . += 0x200;  _und_stack_top = .;
		_sys_stack_bottom = .;  . += 0x1000; _sys_stack_top = .;
		_svc_stack_bottom = .;  . += 0x1000; _svc_stack_top = .;
	}

	ASSERT(. <= 0x88000, "Bootloader exceeds 512 KB reserved region")

	/* Shared flags — 1024 bytes of RAM shared between bootloader and kernel.
	   Both images agree on 0x88000–0x88400; no data is emitted here. */
	.shared_flags 0x88000 (NOLOAD) : {
		_shared_flags_start = .;
		*(.shared_flags)
		_shared_flags_end = .;
	}

	/DISCARD/ : {
		*(*)
	}
}
