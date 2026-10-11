ENTRY(_start)
PHDRS { code PT_LOAD FLAGS(5); data PT_LOAD FLAGS(6); image PT_LOAD FLAGS(4); state PT_LOAD FLAGS(6); }
SECTIONS {
    . = 0x10000;
    __readonly_start = .;
    .text : { *(.text.entry) *(.text*) } :code
    . = ALIGN(4096);
    __text_end = .;
    .rodata : { *(EXCLUDE_FILE (build/curl-blob.o) .rodata*) } :code
    . = ALIGN(4096);
    __readonly_end = .;
    .data : { *(.data*) } :data
    __kernel_payload_end = .;
    ASSERT(__kernel_payload_end <= 0x8fffc, "Kernel exceeds 512 KiB loader capacity")
    .loader_check 0x8fffc : { LONG(0x4e4f544f) } :data
    .curlimage 0x100000 : AT(0x90000) {
        __kernel_static_start = .;
        build/curl-blob.o(.rodata.curlimage)
        . = ALIGN(4096);
    } :image
    __curl_image_end = .;
    ASSERT(__curl_image_end <= 0x14fffc, "Curl exceeds 320 KiB high image capacity")
    .high_check 0x14fffc : AT(0xdfffc) { LONG(0x4e4f544f) } :image
    . = 0x150000;
    __bss_start = .;
    .bss (NOLOAD) : AT(0xe0000) { *(.bss*) *(COMMON) } :state
    __bss_end = .;
    ASSERT(__bss_end < 0x200000, "Kernel BSS exceeds reserved physical region")
    /DISCARD/ : { *(.eh_frame*) *(.comment*) *(.note*) }
}
