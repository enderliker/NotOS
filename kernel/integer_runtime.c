/* Freestanding compiler support: unsigned 64-bit division on a 32-bit CPU. */
#include "base.h"
u64 __udivdi3(u64 numerator,u64 denominator) {
    if (!denominator) __builtin_trap();
    u64 quotient=0,remainder=0;
    for (unsigned bit=64;bit--;) {
        unsigned carry=(unsigned)(remainder>>63);
        remainder=(remainder<<1)|((numerator>>bit)&1);
        if (carry || remainder>=denominator) {remainder-=denominator;quotient|=1ull<<bit;}
    }
    return quotient;
}
