/******************************************************************************
*	kernel.ld
*	 by Alex Chadwick
*
*	A linker script for generation of raspberry pi kernel images.
******************************************************************************/

SECTIONS {
	.init 0x8000 : {
		*(.init)
	}
	
	/* The rest of the code */
	.text : {
		. = ALIGN(4);
		*(.text)
        *(.text.*)		
	}
	
	/* Read-only data */
    .rodata : {
        . = ALIGN(4);
        *(.rodata)
        *(.rodata.*)
    }

	.data : {
        . = ALIGN(4);
		*(.data)
	}

	/* Zero-initialised globals. Startup code zeroes this region before calling kmain */
	.bss : {
        . = ALIGN(4);
		_bss_start = .;
		*(.bss)
		*(COMMON)
		_bss_end = .;
	}

	/* Space for stacks */
	.stacks (NOLOAD) : {
		. = ALIGN(8);
		_fiq_stack_bottom = .;  . += 0x200;  _fiq_stack_top = .;
		_irq_stack_bottom = .;  . += 0x400;  _irq_stack_top = .;
		_abt_stack_bottom = .;  . += 0x200;  _abt_stack_top = .;
		_und_stack_bottom = .;  . += 0x200;  _und_stack_top = .;
		_sys_stack_bottom = .;  . += 0x1000; _sys_stack_top = .;
		_svc_stack_bottom = .;  . += 0x1000; _svc_stack_top = .;
	}

	/*
	* Finally comes everything else. A fun trick here is to put all other
	* sections into this section, which will be discarded by default.
	*/
	/DISCARD/ : {
		*(*)
	}
}
