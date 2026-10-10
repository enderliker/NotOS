ENTRY(_start)
PHDRS { code PT_LOAD FLAGS(5); data PT_LOAD FLAGS(6); }
SECTIONS {
    . = 0x10000;
    __readonly_start = .;
    .text : { *(.text.entry) *(.text*) } :code
    .rodata : { *(.rodata*) } :code
    . = ALIGN(4096);
    __readonly_end = .;
    .data : { *(.data*) } :data
    __kernel_payload_end = .;
    ASSERT(__kernel_payload_end <= 0x3fffc, "Kernel exceeds 192 KiB loader capacity")
    .loader_check 0x3fffc : { LONG(0x4e4f544f) } :data
    __bss_start = .;
    .bss (NOLOAD) : { *(.bss*) *(COMMON) } :data
    __bss_end = .;
    ASSERT(__bss_end < 0x80000, "Kernel overlaps stack guard")
    /DISCARD/ : { *(.eh_frame*) *(.comment*) *(.note*) }
}
