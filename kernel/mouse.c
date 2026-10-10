#include "mouse.h"
#include "mouse_decode.h"
#include "gui.h"
#define QUEUE_BYTES 128u
static u8 queue[QUEUE_BYTES];
static volatile unsigned head, tail, dropped;
static struct mouse_decoder decoder;
static int ready, x = 160, y = 100;
static unsigned buttons, packets;
static int write_port(u16 port, u8 value) {
    for (unsigned i = 0; i < 100000; ++i) if (!(inb(0x64) & 2)) {outb(port,value);return 1;}
    return 0;
}
static int read_port(u8 *byte) {
    for (unsigned i = 0; i < 100000; ++i) if (inb(0x64) & 1) {*byte=inb(0x60);return 1;}
    return 0;
}
static int device_command(u8 value) {
    u8 ack;
    return write_port(0x64,0xd4) && write_port(0x60,value) && read_port(&ack) && ack==0xfa;
}
int mouse_init(void) {
    u8 config;
    ready = 0;
    if (!write_port(0x64,0xa8) || !write_port(0x64,0x20) || !read_port(&config)) return 0;
    config = (u8)((config | 2) & ~0x20);
    if (!write_port(0x64,0x60) || !write_port(0x60,config)
        || !device_command(0xf6) || !device_command(0xf4)) return 0;
    ready = 1; gui_pointer(x,y,0); return 1;
}
void mouse_receive(u8 byte) {
    if (!ready) return;
    unsigned next = (head + 1) & (QUEUE_BYTES-1);
    if (next == tail) {dropped++;return;}
    queue[head] = byte; __asm__ volatile("" ::: "memory"); head = next;
}
void mouse_poll(void) {
    for (unsigned budget = 0; budget < QUEUE_BYTES; ++budget) {
        u32 flags = irq_save();
        if (head == tail) {irq_restore(flags);return;}
        u8 byte = queue[tail]; tail = (tail + 1) & (QUEUE_BYTES-1); irq_restore(flags);
        struct mouse_motion motion;
        if (!mouse_decode(&decoder,byte,&motion)) continue;
        x += motion.dx; y += motion.dy;
        if (x < 0) x = 0; else if (x > 319) x = 319;
        if (y < 0) y = 0; else if (y > 199) y = 199;
        buttons = motion.buttons; packets++;
        gui_pointer(x,y,buttons);
    }
}
void mouse_status(void) {
    console_puts("MOUSE: ready="); console_uint((u32)ready);
    console_puts(" x="); console_int(x); console_puts(" y="); console_int(y);
    console_puts(" buttons="); console_uint(buttons);
    console_puts(" packets="); console_uint(packets);
    console_puts(" dropped="); console_uint(dropped); console_putc('\n');
}
