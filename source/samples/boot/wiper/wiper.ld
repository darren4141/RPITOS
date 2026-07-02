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

	ASSERT(. <= 0x10000, "Wiper exceeds 32 KB — overlaps bootloader load address")

	.shared_flags 0x88000 (NOLOAD) : {
		_shared_flags_start = .;
		*(.shared_flags)
		_shared_flags_end = .;
	}

	/* DWARF debug info — address 0, not loaded, kept only for gdb to read
	   from the .elf. Without these explicit rules, /DISCARD/ below silently
	   throws them away (they're not linker-generated like .symtab/.strtab),
	   which is why file:line breakpoints and source-level stepping stop
	   working even though -g is passed at compile time. */
	.debug_info     0 : { *(.debug_info) }
	.debug_abbrev   0 : { *(.debug_abbrev) }
	.debug_line     0 : { *(.debug_line) }
	.debug_str      0 : { *(.debug_str) }
	.debug_loc      0 : { *(.debug_loc) }
	.debug_loclists 0 : { *(.debug_loclists) }
	.debug_ranges   0 : { *(.debug_ranges) }
	.debug_rnglists 0 : { *(.debug_rnglists) }
	.debug_aranges  0 : { *(.debug_aranges) }
	.debug_frame    0 : { *(.debug_frame) }
	.comment        0 : { *(.comment) }

	/DISCARD/ : {
		*(*)
	}
}
