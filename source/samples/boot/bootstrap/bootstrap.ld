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

		/* Secondary-core (1..3) banked stacks — one region per mode, each
		   sized for 3 cores, indexed by (coreid-1)*<per-core size> in
		   _secondary_boot$. These are resident for the secondary's entire
		   lifetime (a released core keeps running on them, never re-entering
		   any stack-setup code of its own) — see bootstrap docs.md.
		   IRQ needs real per-tick capacity (timer_tick_handler +
		   scheduler_switch_context run on it every tick once the app's real
		   vector table is live). SVC only needs bring-up capacity — it's
		   abandoned the moment the core's first task starts (the scheduler
		   switches SP to that task's own heap-allocated stack). ABT/UND need
		   enough for one uart_fault_report() call each, now that VBAR points
		   at the app's real fault handlers instead of the bootstrap's stub
		   table. FIQ/SYS are never actually entered at runtime (no FIQ
		   source is routed; nothing switches to SYS mode) — minimal
		   placeholders only. */
		/* Sizes are kept as clean powers of two — _secondary_boot$ indexes
		   each region with a single `lsl` per mode, not a general multiply. */
		_sec_fiq_stack_bottom = .;  . += 3 * 0x40;  _sec_fiq_stack_top = .;
		_sec_irq_stack_bottom = .;  . += 3 * 0x400; _sec_irq_stack_top = .;
		_sec_abt_stack_bottom = .;  . += 3 * 0x100; _sec_abt_stack_top = .;
		_sec_und_stack_bottom = .;  . += 3 * 0x80;  _sec_und_stack_top = .;
		_sec_sys_stack_bottom = .;  . += 3 * 0x40;  _sec_sys_stack_top = .;
		_sec_svc_stack_bottom = .;  . += 3 * 0x400; _sec_svc_stack_top = .;
	}

	/* Bootstrap must not grow into the bootloader load region at 0x10000 */
	ASSERT(. <= 0x10000, "Bootstrap exceeds 32 KB — overlaps bootloader load address")

	/* Shared flags region — no data emitted, symbols needed by boot_flags.c
	   if it is ever added to the bootstrap. */
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
