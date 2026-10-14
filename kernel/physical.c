#include "physical.h"
struct boot_memory {u32 magic,count;struct e820_entry entries[64];} __attribute__((packed));
static struct frame_pool pool;
static u32 test_frames[FRAME_COUNT];
static unsigned map_count;
extern char __kernel_static_start[],__bss_end[];
int physical_init(void) {
    const struct boot_memory *map=(const struct boot_memory *)0x5000;
    if(map->magic!=0x4e4d4150 || !map->count || map->count>64) return 0;
    if(!frame_pool_init(&pool,map->entries,map->count)) return 0;
    if (!frame_reserve(&pool,(u32)__kernel_static_start,(u32)(__bss_end-__kernel_static_start))) return 0;
    map_count=map->count;return 1;
}
u32 physical_alloc(void) {u32 flags=irq_save();u32 p=frame_take(&pool);irq_restore(flags);return p;}
int physical_free(u32 address) {u32 flags=irq_save();int ok=frame_release(&pool,address);irq_restore(flags);return ok;}
int physical_usable(u32 address) {return frame_usable(&pool,address);}
int physical_owned(u32 address) {u32 flags=irq_save();int ok=frame_owned(&pool,address);irq_restore(flags);return ok;}
unsigned physical_available(void) {u32 flags=irq_save();unsigned available=pool.available;irq_restore(flags);return available;}
void physical_status(void) {
    console_puts("MEM: E820 entries=");console_uint(map_count);
    console_puts(" managed_frames=");console_uint(pool.total);
    console_puts(" free_frames=");console_uint(pool.available);
    console_puts(" cap_MiB=64\n");
}
int physical_selftest(void) {
    unsigned before=physical_available(),count=0;int ok=1;
    while(count<FRAME_COUNT) {
        u32 address=physical_alloc();if(!address) break;
        test_frames[count++]=address;
        if(address<0x100000 || (address&4095)) ok=0;
        volatile u32 *page=(volatile u32 *)address;
        page[0]=address^0x55aa55aa;page[1023]=~address;
    }
    if(count!=before || physical_alloc()!=0 || physical_available()!=0) ok=0;
    if(physical_free(0)||physical_free(0x100001)||physical_free(PHYSICAL_LIMIT)) ok=0;
    for(unsigned i=0;i<count;i++) {
        volatile u32 *page=(volatile u32 *)test_frames[i];
        if(page[0]!=(test_frames[i]^0x55aa55aa)||page[1023]!=~test_frames[i]) ok=0;
        if(!physical_free(test_frames[i])||physical_free(test_frames[i])) ok=0;
    }
    if(physical_available()!=before) ok=0;
    return ok;
}
