#include "base.h"
#include "clock.h"
static u16 previous;
static unsigned fraction,elapsed;
static u16 counter(void) {
    outb(0x43,0);
    u8 low=inb(0x40),high=inb(0x40);
    return (u16)(low|((u16)high<<8));
}
void clock_init(void) {
    outb(0x43,0x34);outb(0x40,0);outb(0x40,0);
    previous=counter();
}
unsigned clock_ms(void) {
    /* Call more often than one PIT wrap (~55 ms); this is a polling prototype. */
    u16 current=counter();
    fraction+=(u16)(previous-current);previous=current;
    elapsed+=fraction/1193;fraction%=1193;
    return elapsed;
}
