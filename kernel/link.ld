ENTRY(_start)
SECTIONS {
    . = 0x10000;
    .text : { *(.text.entry) *(.text*) }
    .rodata : { *(.rodata*) }
    .data : { *(.data*) }
    __kernel_payload_end = .;
    ASSERT(__kernel_payload_end <= 0x1fffc, "Kernel exceeds 64 KiB loader capacity")
    .loader_check 0x1fffc : { LONG(0x4e4f544f) }
    __bss_start = .;
    .bss : { *(.bss*) *(COMMON) }
    __bss_end = .;
    ASSERT(__bss_end < 0x80000, "Kernel overlaps stack")
    /DISCARD/ : { *(.eh_frame*) *(.comment*) *(.note*) }
}
