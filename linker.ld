ENTRY(_start)

SECTIONS {
    . = 0x10000;

    _kernel_start = .;

    .text.entry : { *(.text.entry) }
    .multiboot : { KEEP(*(.multiboot)) }
    .text : { *(.text) }
    .rodata : { *(.rodata) }
    .data   : { *(.data) }

    _kernel_raw_end = .;

    . = 0x800000;

    .bss : {
        _bss_start = .;
        *(COMMON)
        *(.bss)
        _bss_end = .;
    }

    . = ALIGN(16);
    stack_bottom = .;
    . += 16384;
    stack_top = .;

    _kernel_end = .;
}