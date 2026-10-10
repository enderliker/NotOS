typedef unsigned char u8;
typedef unsigned short u16;
static void outb(u16 port, u8 value) { __asm__ volatile("outb %0,%1" :: "a"(value), "Nd"(port)); }
static u8 inb(u16 port) { u8 value; __asm__ volatile("inb %1,%0" : "=a"(value) : "Nd"(port)); return value; }
static unsigned cursor;
static void putc(char c) {
    while (!(inb(0x3fd) & 0x20)) {}
    outb(0x3f8, (u8)c);
    volatile u16 *vga = (volatile u16 *)0xb8000;
    if (c == '\n') cursor = (cursor / 80 + 1) * 80;
    else if (cursor < 80 * 25) vga[cursor++] = 0x0f00 | (u8)c;
}
static void puts(const char *s) { while (*s) putc(*s++); }
void kernel_main(void) {
    outb(0x3f9, 0); outb(0x3fb, 0x80); outb(0x3f8, 1); outb(0x3f9, 0);
    outb(0x3fb, 3); outb(0x3fa, 0xc7); outb(0x3fc, 0x0b);
    volatile u16 *vga = (volatile u16 *)0xb8000;
    for (unsigned i = 0; i < 2000; ++i) vga[i] = 0x0720;
    puts("NotOS 0.1\nBOOT: protected mode OK\nBOOT: kernel C OK\n");
    for (;;) __asm__ volatile("pause");
}
