ENTRY(_start)
PHDRS { code PT_LOAD FLAGS(5); data PT_LOAD FLAGS(6); }
SECTIONS {
    . = 0x10000;
    __readonly_start = .;
    .text : { *(.text.entry) *(.text*) } :code
    . = ALIGN(4096);
    __text_end = .;
    .rodata : { *(.rodata*) } :code
    . = ALIGN(4096);
    __readonly_end = .;
    .data : { *(.data*) } :data
    __kernel_payload_end = .;
    ASSERT(__kernel_payload_end <= 0x8fffc, "Kernel exceeds 512 KiB loader capacity")
    .loader_check 0x8fffc : { LONG(0x4e4f544f) } :data
    . = 0x100000;
    __bss_start = .;
    .bss (NOLOAD) : { *(.bss*) *(COMMON) } :data
    __bss_end = .;
    ASSERT(__bss_end < 0x200000, "Kernel BSS exceeds reserved physical region")
    /DISCARD/ : { *(.eh_frame*) *(.comment*) *(.note*) }
}
