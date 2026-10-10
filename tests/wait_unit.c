#include <assert.h>
#include <stdio.h>
#include "../kernel/wait_policy.h"
int main(void) {
    u32 starts[]={0,1,0x7ffffffeu,0xfffffffeu};
    u32 durations[]={0,1,500,WAIT_MAX_MS};
    for (unsigned i=0;i<4;i++) for (unsigned j=0;j<4;j++) {
        u32 start=starts[i],duration=durations[j];
        assert(wait_due(start,duration,start+duration));
        assert(wait_due(start,duration,start+duration+1));
        if (duration) assert(!wait_due(start,duration,start+duration-1));
    }
    assert(!wait_due(0xffffff00u,600,0x50));
    assert(wait_due(0xffffff00u,600,0x158));
    puts("PASS: relative wait deadlines, limits and unsigned clock wrap");
}
