#include <assert.h>
#include <stdio.h>
#include "../kernel/physical_core.h"
static struct frame_pool pool;
int main(void) {
    struct e820_entry e[]={
        {0,32u*1024u*1024u,1,1},
        {0x200123,123,2,1}, /* A partial reserved page excludes the whole page. */
        {0x300000,4096,1,0}, /* Disabled entries ignored. */
        {0x400001,8190,2,1},
    };
    assert(frame_pool_init(&pool,e,4));
    assert(!frame_usable(&pool,0x200000));
    assert(!frame_usable(&pool,0x400000));assert(!frame_usable(&pool,0x401000));
    assert(!frame_usable(&pool,0));assert(!frame_usable(&pool,0x100001));
    unsigned expected=32u*1024u*1024u/4096-256-3;
    assert(pool.available==expected && pool.total==expected);
    u32 a=frame_take(&pool);assert(a==0x100000 && frame_owned(&pool,a));
    assert(!frame_release(&pool,a+1));assert(!frame_release(&pool,0));
    assert(frame_release(&pool,a));assert(!frame_release(&pool,a));
    assert(frame_take(&pool)==a);assert(frame_release(&pool,a));
    unsigned taken=0;u32 last=0;
    while((a=frame_take(&pool))) {assert(a>last && !(a&4095));last=a;taken++;}
    assert(taken==expected && !pool.available && !frame_take(&pool));
    assert(frame_release(&pool,last) && frame_take(&pool)==last);
    struct e820_entry huge={0,~0ull,1,1};
    assert(frame_pool_init(&pool,&huge,1));assert(pool.total==FRAME_COUNT-256);
    struct e820_entry empty={0,0,1,1};
    assert(!frame_pool_init(&pool,&empty,1));
    assert(!frame_pool_init(&pool,&huge,65));
    struct e820_entry unaligned={0x100001,8190,1,1};
    assert(!frame_pool_init(&pool,&unaligned,1));
    unaligned.size=8191;
    assert(frame_pool_init(&pool,&unaligned,1));assert(pool.total==1);
    assert(frame_take(&pool)==0x101000);
    puts("PASS: E820 alignment/overlap/cap, frame exhaustion, reuse and invalid free");
}
