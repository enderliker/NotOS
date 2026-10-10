ENTRY(_start)
PHDRS { code PT_LOAD FLAGS(5); readonly PT_LOAD FLAGS(4); data PT_LOAD FLAGS(6); }
SECTIONS {
    . = 0x08000000;
    .text : { *(.text._start) *(.text*) } :code
    . = ALIGN(4096);
    .rodata : { *(.rodata*) } :readonly
    .init_array : {
        __init_array_start = .;
        KEEP(*(SORT_BY_INIT_PRIORITY(.init_array.*))) KEEP(*(.init_array))
        __init_array_end = .;
    } :readonly
    .fini_array : {
        __fini_array_start = .;
        KEEP(*(SORT_BY_INIT_PRIORITY(.fini_array.*))) KEEP(*(.fini_array))
        __fini_array_end = .;
    } :readonly
    . = ALIGN(4096);
    .data : { *(.data*) } :data
    .bss (NOLOAD) : { *(.bss*) *(COMMON) } :data
    ASSERT(. <= 0x08400000, "ELF image crosses the private heap")
    /DISCARD/ : { *(.eh_frame*) *(.comment*) *(.note*) }
}
