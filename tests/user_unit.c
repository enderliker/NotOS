#include <assert.h>
#include <stdio.h>
#include "../kernel/user_memory.h"
static struct page_entry table[512];
int main(void) {
    table[0].low=0x100005;table[1].low=0x101007;table[3].low=0x102007;
    assert(user_memory_check(table,7,USER_BASE,4096,0));
    assert(!user_memory_check(table,7,USER_BASE,1,1));
    assert(user_memory_check(table,7,USER_BASE+4096,4096,1));
    assert(user_memory_check(table,7,USER_BASE+4090,12,0));
    assert(!user_memory_check(table,7,USER_BASE+8190,4,0));
    assert(!user_memory_check(table,7,0x10000,4,0));
    assert(!user_memory_check(table,7,0xfffffff8,16,0));
    assert(!user_memory_check(table,7,USER_BASE,0xffffffffu,0));
    assert(!user_memory_check(table,3,USER_BASE,1,0));
    assert(!user_memory_check(table,5,USER_BASE+4096,1,1));
    assert(user_memory_check(table,7,0xffffffffu,0,0));
    table[1].low&=~4u;assert(!user_memory_check(table,7,USER_BASE+4096,1,0));
    puts("PASS: whole-span user pointer bounds, permissions, guards and overflow");
}
