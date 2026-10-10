#include <assert.h>
#include <stdio.h>
#include "../kernel/heap_core.h"
static u8 arena[1024*1024] __attribute__((aligned(16)));
static struct heap_pool heap;
static u32 seed=0x12345678;
static u32 random_value(void) {seed=seed*1664525u+1013904223u;return seed;}
int main(void) {
    assert(!heap_pool_init(&heap,arena+1,sizeof(arena)-1));
    assert(!heap_pool_init(&heap,arena,16));
    assert(heap_pool_init(&heap,arena,sizeof(arena)) && heap_valid(&heap));
    struct heap_info before,after;assert(heap_inspect(&heap,&before));
    assert(!heap_take(&heap,0));assert(!heap_take(&heap,0xffffffffu));
    assert(!heap_zero(&heap,0xffffffffu,2));assert(!heap_zero(&heap,0,1));
    u8 *a=heap_take(&heap,33),*b=heap_take(&heap,4097),*c=heap_take(&heap,17);
    assert(a&&b&&c);assert(!(((unsigned long)a|(unsigned long)b|(unsigned long)c)&15));
    assert(!heap_release(&heap,a+16));assert(!heap_release(&heap,a+1));assert(!heap_release(&heap,(void *)1));
    for(unsigned i=0;i<4097;i++) b[i]=0x77;
    assert(heap_release(&heap,b)&&!heap_release(&heap,b));
    u8 *zero=heap_zero(&heap,64,2);assert(zero==b);
    for(unsigned i=0;i<128;i++) assert(zero[i]==0);
    assert(heap_release(&heap,a)&&heap_release(&heap,c)&&heap_release(&heap,zero));
    assert(heap_release(&heap,0)&&heap_inspect(&heap,&after));
    assert(after.free_bytes==before.free_bytes&&after.largest_free==before.largest_free&&!after.allocations);
    void *whole=heap_take(&heap,before.largest_free);assert(whole&&!heap_take(&heap,1));
    assert(heap_release(&heap,whole));
    struct {u8 *p;u32 size;u8 pattern;} slots[64]={0};
    for(unsigned iteration=0;iteration<20000;iteration++) {
        unsigned index=random_value()%64;
        if(slots[index].p) {
            for(unsigned j=0;j<slots[index].size;j++) assert(slots[index].p[j]==slots[index].pattern);
            assert(heap_release(&heap,slots[index].p));slots[index].p=0;
        } else {
            u32 n=random_value()%4096+1;u8 *p=heap_take(&heap,n);assert(p);
            slots[index].p=p;slots[index].size=n;slots[index].pattern=(u8)(iteration+1);
            for(unsigned j=0;j<n;j++) p[j]=slots[index].pattern;
        }
        assert(heap_valid(&heap));
    }
    for(unsigned i=0;i<64;i++) if(slots[i].p) assert(heap_release(&heap,slots[i].p));
    assert(heap_inspect(&heap,&after));
    assert(after.free_bytes==before.free_bytes&&after.largest_free==before.largest_free&&!after.allocations);
    u8 saved=arena[0];arena[0]^=1; /* Corrupt size alignment, not payload. */
    assert(!heap_valid(&heap)&&!heap_take(&heap,1));
    arena[0]=saved;assert(heap_valid(&heap));
    puts("PASS: heap split/coalesce/zero/OOM/invalid free, metadata validation and 20000 allocation operations");
}
