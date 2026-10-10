/* Pure bitmap allocator shared by native tests and the freestanding kernel. */
#include "physical_core.h"
static int bit(const u32 *map,unsigned n) {return (map[n/32]>>(n%32))&1;}
static void set(u32 *map,unsigned n,int value) {
    if(value) map[n/32]|=1u<<(n%32);else map[n/32]&=~(1u<<(n%32));
}
int frame_pool_init(struct frame_pool *p,const struct e820_entry *entries,unsigned count) {
    if(!p||!entries||!count||count>64) return 0;
    p->total=p->available=0;
    for(unsigned i=0;i<FRAME_COUNT/32;i++) p->usable[i]=p->free[i]=0;
    /* Reserved regions win over overlapping available regions, regardless of order. */
    for(unsigned pass=0;pass<2;pass++) for(unsigned i=0;i<count;i++) {
        const struct e820_entry *e=&entries[i];
        if(!(e->attributes&1)||!e->size||e->base>=PHYSICAL_LIMIT) continue;
        if((e->type==1)!=(pass==0)) continue;
        u32 lo=(u32)e->base;
        u32 hi=e->size>=PHYSICAL_LIMIT-lo?PHYSICAL_LIMIT:lo+(u32)e->size;
        unsigned first,last;
        if(pass==0) {first=(lo+PAGE_SIZE-1)/PAGE_SIZE;last=hi/PAGE_SIZE;}
        else {first=lo/PAGE_SIZE;last=(hi+PAGE_SIZE-1)/PAGE_SIZE;}
        for(unsigned n=first;n<last;n++) set(p->usable,n,pass==0);
    }
    /* Low 1 MiB: firmware, VGA, boot map, kernel/BSS, GDT/IDT and boot stack. */
    for(unsigned n=0;n<1024u*1024u/PAGE_SIZE;n++) set(p->usable,n,0);
    for(unsigned n=0;n<FRAME_COUNT;n++) if(bit(p->usable,n)) {set(p->free,n,1);p->total++;}
    p->available=p->total;
    return p->total!=0;
}
u32 frame_take(struct frame_pool *p) {
    if(!p->available) return 0;
    for(unsigned word=0;word<FRAME_COUNT/32;word++) if(p->free[word]) {
        unsigned b=(unsigned)__builtin_ctz(p->free[word]);
        p->free[word]&=~(1u<<b);p->available--;
        return (word*32+b)*PAGE_SIZE;
    }
    return 0;
}
int frame_usable(const struct frame_pool *p,u32 address) {
    return address<PHYSICAL_LIMIT && !(address&(PAGE_SIZE-1)) && bit(p->usable,address/PAGE_SIZE);
}
int frame_owned(const struct frame_pool *p,u32 address) {
    return frame_usable(p,address) && !bit(p->free,address/PAGE_SIZE);
}
int frame_release(struct frame_pool *p,u32 address) {
    if(!frame_owned(p,address)) return 0;
    set(p->free,address/PAGE_SIZE,1);p->available++;return 1;
}
