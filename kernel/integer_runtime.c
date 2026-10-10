/* Freestanding compiler support shared by kernel and i386 libc applications. */
#include "base.h"
u64 __udivmoddi4(u64 numerator,u64 denominator,u64 *output_remainder) {
    if (!denominator) __builtin_trap();
    u64 quotient=0,remainder=0;
    for (unsigned bit=64;bit--;) {
        unsigned carry=(unsigned)(remainder>>63);
        remainder=(remainder<<1)|((numerator>>bit)&1);
        if (carry || remainder>=denominator) {remainder-=denominator;quotient|=1ull<<bit;}
    }
    if (output_remainder) *output_remainder=remainder;
    return quotient;
}
u64 __udivdi3(u64 numerator,u64 denominator) {return __udivmoddi4(numerator,denominator,0);}
u64 __umoddi3(u64 numerator,u64 denominator) {
    u64 remainder;__udivmoddi4(numerator,denominator,&remainder);return remainder;
}
long long __divmoddi4(long long numerator,long long denominator,long long *output_remainder) {
    u64 n=numerator<0?-(u64)numerator:(u64)numerator;
    u64 d=denominator<0?-(u64)denominator:(u64)denominator,remainder;
    u64 quotient=__udivmoddi4(n,d,&remainder);
    if (output_remainder) *output_remainder=(long long)(numerator<0?-remainder:remainder);
    return (long long)((numerator<0)!=(denominator<0)?-quotient:quotient);
}
long long __divdi3(long long numerator,long long denominator) {return __divmoddi4(numerator,denominator,0);}
long long __moddi3(long long numerator,long long denominator) {
    long long remainder;__divmoddi4(numerator,denominator,&remainder);return remainder;
}
