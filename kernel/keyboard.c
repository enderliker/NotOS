#include "base.h"
#include "keyboard.h"
#include "mouse.h"
#define CAPACITY 256u
static u8 queue[CAPACITY];
static volatile unsigned head,tail,dropped;
static int writable(void) {
    for(unsigned i=0;i<100000;i++) if(!(inb(0x64)&2)) return 1;
    return 0;
}
static int command(u8 value) {if(!writable()) return 0;outb(0x64,value);return 1;}
static int data(u8 value) {if(!writable()) return 0;outb(0x60,value);return 1;}
static int response(u8 *value) {
    for(unsigned i=0;i<100000;i++) if(inb(0x64)&1) {*value=inb(0x60);return 1;}
    return 0;
}
int keyboard_init(void) {
    head=tail=dropped=0;
    if(!command(0xad)||!command(0xa7)) return 0;
    for(unsigned i=0;i<32 && (inb(0x64)&1);i++) (void)inb(0x60);
    u8 config,ack;
    if(!command(0x20)||!response(&config)) return 0;
    config=(u8)((config|0x61)&~0x12); /* IRQ1 + set-1 translation; IRQ12/port2 disabled. */
    if(!command(0x60)||!data(config)||!command(0xae)) return 0;
    if(!data(0xf4)||!response(&ack)||ack!=0xfa) return 0;
    return 1;
}
void keyboard_irq(void) {
    /* Queue raw scancodes only; no GUI, parser or console calls from an ISR. */
    for(unsigned budget=0;budget<32;budget++) {
        u8 status=inb(0x64);
        if(!(status&1)) return;
        u8 code=inb(0x60);
        if(status&0xc0) continue; /* parity/timeout */
        if(status&0x20) {mouse_receive(code);continue;}
        if(code==0xfa||code==0xfe) continue;
        unsigned next=(head+1)&(CAPACITY-1);
        if(next==tail) {dropped++;continue;}
        queue[head]=code;
        __asm__ volatile("" ::: "memory");
        head=next;
    }
}
int keyboard_next(u8 *code) {
    u32 flags=irq_save();
    if(head==tail) {irq_restore(flags);return 0;}
    *code=queue[tail];tail=(tail+1)&(CAPACITY-1);
    irq_restore(flags);return 1;
}
unsigned keyboard_dropped(void) {return dropped;}
