#include <assert.h>
#include <stdio.h>
#include "../kernel/base.h"
u64 __udivdi3(u64 numerator,u64 denominator);
u64 __umoddi3(u64 numerator,u64 denominator);
u64 __udivmoddi4(u64 numerator,u64 denominator,u64 *remainder);
long long __divdi3(long long numerator,long long denominator);
long long __moddi3(long long numerator,long long denominator);
long long __divmoddi4(long long numerator,long long denominator,long long *remainder);
int main(void) {
    u64 values[]={0,1,2,0xffffffffull,0x100000000ull,0x8000000000000000ull,~0ull};
    for (unsigned a=0;a<7;a++) for (unsigned b=1;b<7;b++)
        {
            u64 remainder;assert(__udivmoddi4(values[a],values[b],&remainder)==values[a]/values[b]);
            assert(remainder==values[a]%values[b] && __umoddi3(values[a],values[b])==remainder);
        }
    long long signed_values[]={(-9223372036854775807LL-1),-9223372036854775807LL,-4294967296LL,-1,0,1,4294967296LL,9223372036854775807LL};
    for (unsigned a=0;a<8;a++) for (unsigned b=0;b<8;b++) {
        long long n=signed_values[a],d=signed_values[b],remainder;
        if (!d || (n==signed_values[0] && d==-1)) continue;
        assert(__divmoddi4(n,d,&remainder)==n/d && remainder==n%d);
        assert(__divdi3(n,d)==n/d && __moddi3(n,d)==n%d);
    }
    u64 seed=9;
    for (unsigned i=0;i<100000;i++) {
        seed=seed*6364136223846793005ull+1;u64 numerator=seed;
        seed=seed*6364136223846793005ull+1;u64 denominator=seed|1;
        assert(__udivdi3(numerator,denominator)==numerator/denominator);
        assert(__umoddi3(numerator,denominator)==numerator%denominator);
        long long n=(long long)numerator,d=(long long)denominator,remainder;
        if (!(n==(-9223372036854775807LL-1) && d==-1)) {
            assert(__divmoddi4(n,d,&remainder)==n/d && remainder==n%d);
            assert(__divdi3(n,d)==n/d && __moddi3(n,d)==n%d);
        }
    }
    puts("PASS: portable signed/unsigned u64 division/remainder boundaries and 100000 random comparisons");
}
