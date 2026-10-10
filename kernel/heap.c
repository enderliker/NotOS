#include "heap.h"
#include "paging.h"
#include "physical.h"
static struct heap_pool pool;
static void *slots[8];
int heap_init(void) {
    unsigned mapped=0;
    while(mapped<HEAP_BYTES/PAGE_SIZE) {
        u32 physical=physical_alloc();
        if(!physical) break;
        u32 virtual_address=HEAP_BASE+mapped*PAGE_SIZE;
        if(!paging_map(virtual_address,physical,1)) {physical_free(physical);break;}
        volatile u32 *page=(volatile u32 *)virtual_address;
        for(unsigned i=0;i<1024;i++) page[i]=0;
        mapped++;
    }
    if(mapped!=HEAP_BYTES/PAGE_SIZE) {
        while(mapped) {mapped--;physical_free(paging_unmap(HEAP_BASE+mapped*PAGE_SIZE));}
        return 0;
    }
    return heap_pool_init(&pool,(void *)HEAP_BASE,HEAP_BYTES);
}
void *kmalloc(u32 bytes) {u32 flags=irq_save();void *p=heap_take(&pool,bytes);irq_restore(flags);return p;}
void *kcalloc(u32 count,u32 bytes) {
    if(bytes&&count>0xffffffffu/bytes) return 0;
    u32 total=count*bytes;u8 *p=kmalloc(total);
    /* Own allocation is private; avoid a large zero-fill with IRQ disabled. */
    if(p) for(u32 i=0;i<total;i++) p[i]=0;
    return p;
}
int kfree(void *p) {u32 flags=irq_save();int ok=heap_release(&pool,p);irq_restore(flags);return ok;}
static int inspect(struct heap_info *info) {u32 flags=irq_save();int ok=heap_inspect(&pool,info);irq_restore(flags);return ok;}
void heap_status(void) {
    struct heap_info info;
    if(!inspect(&info)) {console_puts("HEAP ERROR: corrupt metadata\n");return;}
    console_puts("HEAP: bytes=");console_uint(HEAP_BYTES);
    console_puts(" free_bytes=");console_uint(info.free_bytes);
    console_puts(" used_bytes=");console_uint(info.used_bytes);
    console_puts(" allocations=");console_uint(info.allocations);
    console_puts(" largest_free=");console_uint(info.largest_free);console_putc('\n');
}
int heap_selftest(void) {
    struct heap_info before,after;
    if(!inspect(&before)||before.allocations) return 0;
    unsigned physical_before=physical_available();int ok=1;
    u8 *a=kmalloc(33),*b=kmalloc(4097),*c=kmalloc(80),*zero=0;
    if(!a||!b||!c) ok=0;
    if(ok) {
        if(((u32)a|(u32)b|(u32)c)&15) ok=0;
        for(unsigned i=0;i<33;i++) a[i]=0x41;
        for(unsigned i=0;i<4097;i++) b[i]=0x42;
        for(unsigned i=0;i<80;i++) c[i]=0x43;
        if(kfree(a+16)||kfree(a+1)||kfree((void *)0x10000)) ok=0;
        if(!kfree(b)||kfree(b)) ok=0;
        b=0;zero=kcalloc(64,2);
        if(!zero) ok=0;
        else for(unsigned i=0;i<128;i++) if(zero[i]) ok=0;
        for(unsigned i=0;i<33;i++) if(a[i]!=0x41) ok=0;
        for(unsigned i=0;i<80;i++) if(c[i]!=0x43) ok=0;
    }
    if(a&&!kfree(a)) ok=0;
    if(b&&!kfree(b)) ok=0;
    if(c&&!kfree(c)) ok=0;
    if(zero&&!kfree(zero)) ok=0;
    if(!kfree(0)||kmalloc(0)||kmalloc(0xffffffffu)||kcalloc(0xffffffffu,2)) ok=0;
    if(!inspect(&after)||after.free_bytes!=before.free_bytes||after.allocations) ok=0;
    void *whole=kmalloc(before.largest_free);
    if(!whole) ok=0;
    else {void *extra=kmalloc(1);if(extra) {ok=0;kfree(extra);}if(!kfree(whole)) ok=0;}
    if(!inspect(&after)||after.free_bytes!=before.free_bytes||after.largest_free!=before.largest_free||after.allocations||physical_available()!=physical_before) ok=0;
    return ok;
}
static int number(const char *s,u32 *value) {
    if(!*s) return 0;
    u32 n=0;
    while(*s) {
        if(*s<'0'||*s>'9') return 0;
        unsigned d=(unsigned)(*s++-'0');
        if(n>(0xffffffffu-d)/10) return 0;
        n=n*10+d;
    }
    *value=n;return 1;
}
void heap_command(const char *kind,const char *argument) {
    u32 n;
    if(!number(argument,&n)) {console_puts("HEAP ERROR: invalid number\n");return;}
    if(equal(kind,"alloc")) {
        unsigned slot=0;while(slot<8&&slots[slot]) slot++;
        if(slot==8) {console_puts("HEAP ERROR: all slots occupied\n");return;}
        void *p=kcalloc(1,n);
        if(!p) {console_puts("HEAP ERROR: allocation failed\n");return;}
        slots[slot]=p;console_puts("HEAP: slot=");console_uint(slot);
        console_puts(" bytes=");console_uint(n);console_putc('\n');
    } else {
        if(n>=8||!slots[n]) {console_puts("HEAP ERROR: invalid or empty slot\n");return;}
        if(!kfree(slots[n])) {console_puts("HEAP ERROR: free failed\n");return;}
        slots[n]=0;console_puts("HEAP: freed slot=");console_uint(n);console_putc('\n');
    }
}
void heap_fault_guard_end(void) {
    __asm__ volatile("movb $1,(%0)" :: "r"(HEAP_BASE+HEAP_BYTES) : "memory");
}
