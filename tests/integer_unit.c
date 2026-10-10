#include <assert.h>
#include <stdio.h>
#include "../kernel/base.h"
u64 __udivdi3(u64 numerator,u64 denominator);
int main(void) {
    u64 values[]={0,1,2,0xffffffffull,0x100000000ull,0x8000000000000000ull,~0ull};
    for (unsigned a=0;a<7;a++) for (unsigned b=1;b<7;b++)
        assert(__udivdi3(values[a],values[b])==values[a]/values[b]);
    u64 seed=9;
    for (unsigned i=0;i<100000;i++) {
        seed=seed*6364136223846793005ull+1;u64 numerator=seed;
        seed=seed*6364136223846793005ull+1;u64 denominator=seed|1;
        assert(__udivdi3(numerator,denominator)==numerator/denominator);
    }
    puts("PASS: portable u64 division boundaries and 100000 random comparisons");
}
