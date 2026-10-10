#include "user_memory.h"
int user_memory_check(const u32 *table,u32 pde,u32 address,u32 bytes,int writable) {
    if(!bytes) return 1;
    if(!table||(pde&5)!=5||(writable&&!(pde&2))) return 0;
    if(address<USER_BASE||address>=USER_BASE+USER_SIZE) return 0;
    u32 offset=address-USER_BASE;
    if(bytes>USER_SIZE-offset) return 0;
    unsigned first=offset/4096,last=(offset+bytes-1)/4096;
    for(unsigned i=first;i<=last;i++)
        if((table[i]&5)!=5||(writable&&!(table[i]&2))) return 0;
    return 1;
}
