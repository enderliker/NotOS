#include "base.h"
#include "pic.h"
#define MASTER 0x20
#define SLAVE 0xa0
static u16 masks=0xffff;
static void write(u16 port,u8 value) {outb(port,value);outb(0x80,0);}
static void apply_masks(void) {outb(MASTER+1,(u8)masks);outb(SLAVE+1,(u8)(masks>>8));}
void pic_init(void) {
    masks=0xffff;apply_masks();
    write(MASTER,0x11);write(SLAVE,0x11);
    write(MASTER+1,0x20);write(SLAVE+1,0x28);
    write(MASTER+1,4);write(SLAVE+1,2);
    write(MASTER+1,1);write(SLAVE+1,1);
    apply_masks();
}
void pic_unmask(unsigned irq) {if(irq<16) {masks&=(u16)~(1u<<irq);apply_masks();}}
void pic_mask(unsigned irq) {if(irq<16) {masks|=(u16)(1u<<irq);apply_masks();}}
void pic_eoi(unsigned irq) {
    /* Specific EOI: don't accidentally acknowledge an unrelated in-service IRQ. */
    if(irq>=8) {outb(SLAVE,(u8)(0x60|(irq-8)));outb(MASTER,0x62);}
    else outb(MASTER,(u8)(0x60|irq));
}
int pic_spurious(unsigned irq) {
    if(irq==7) {outb(MASTER,0x0b);return !(inb(MASTER)&0x80);}
    if(irq==15) {
        outb(SLAVE,0x0b);
        if(!(inb(SLAVE)&0x80)) {outb(MASTER,0x62);return 1;}
    }
    return 0;
}
