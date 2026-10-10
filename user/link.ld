ENTRY(_start)
SECTIONS {
    . = 0x08000000;
    .text : { *(.text._start) *(.text*) }
    .rodata : { *(.rodata*) }
    .data : { *(.data*) }
    .bss : { *(.bss*) *(COMMON) }
    ASSERT(SIZEOF(.bss) == 0, "User VM must not have static mutable data")
    ASSERT(. <= 0x08001000, "User VM exceeds its code page")
    /DISCARD/ : { *(.eh_frame*) *(.comment*) *(.note*) }
}
