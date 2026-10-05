ENTRY(_start)

SECTIONS {
    . = 0x10000;

    _kernel_start = .;

    .text.entry : {
        *(.text.entry)
    }

    .multiboot : {
        KEEP(*(.multiboot))
    }

    .text : {
        *(.text)
    }

    .rodata : { *(.rodata) }
    .data   : { *(.data) }

    .bss : {
        _bss_start = .;
        *(COMMON)
        *(.bss)
        _bss_end = .;
    }

    /* 栈在 .bss 之后独立分配，不占文件空间 */
    . = ALIGN(16);
    stack_bottom = .;
    . += 16384;
    stack_top = .;

    _kernel_end = .;
}