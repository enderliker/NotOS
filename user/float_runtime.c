/* IEEE 754 binary64 soft-float runtime for freestanding i386.
 * Pure integer bit manipulation; no x87, FPU, SSE or host libgcc dependencies.
 * Reference: LLVM compiler-rt builtins (Apache-2.0 with LLVM Exception). */

#ifdef NOTOS_HOST_TEST
#include <stdint.h>
typedef uint8_t u8;
typedef uint16_t u16;
typedef uint32_t u32;
typedef uint64_t u64;
#else
#include "../kernel/base.h"
#endif

#ifdef FLOAT_TEST_PREFIX
#define __floatsidf   test_floatsidf
#define __floatdidf   test_floatdidf
#define __fixdfsi     test_fixdfsi
#define __fixunsdfsi  test_fixunsdfsi
#define __fixdfdi     test_fixdfdi
#define __muldf3      test_muldf3
#define __divdf3      test_divdf3
#define __adddf3      test_adddf3
#define __subdf3      test_subdf3
#define __gtdf2       test_gtdf2
#define __ltdf2       test_ltdf2
#define __gedf2       test_gedf2
#define __ledf2       test_ledf2
#define __eqdf2       test_eqdf2
#define __nedf2       test_nedf2
#endif

#define SIGN_BIT    0x8000000000000000ULL
#define EXP_MASK    0x7FF0000000000000ULL
#define MANT_MASK   0x000FFFFFFFFFFFFFULL
#define CANON_QNAN  0x7FF8000000000000ULL

typedef union {
    double d;
    u64 u;
} du;

__attribute__((always_inline)) static inline int is_nan(u64 u) {
    return (u & EXP_MASK) == EXP_MASK && (u & MANT_MASK) != 0;
}

__attribute__((always_inline)) static inline int is_zero(u64 u) {
    return (u & ~SIGN_BIT) == 0;
}

/* Comparisons */
int __gtdf2(double a, double b) {
    u64 ua = ((du){.d = a}).u;
    u64 ub = ((du){.d = b}).u;
    if (is_nan(ua) || is_nan(ub)) return -1;
    if (is_zero(ua) && is_zero(ub)) return 0;
    u64 sa = ua & SIGN_BIT;
    u64 sb = ub & SIGN_BIT;
    if (sa != sb) return sa ? -1 : 1;
    if (ua == ub) return 0;
    if (!sa) return ua > ub ? 1 : -1;
    return ua > ub ? -1 : 1;
}

int __ltdf2(double a, double b) {
    u64 ua = ((du){.d = a}).u;
    u64 ub = ((du){.d = b}).u;
    if (is_nan(ua) || is_nan(ub)) return 1;
    if (is_zero(ua) && is_zero(ub)) return 0;
    u64 sa = ua & SIGN_BIT;
    u64 sb = ub & SIGN_BIT;
    if (sa != sb) return sa ? -1 : 1;
    if (ua == ub) return 0;
    if (!sa) return ua < ub ? -1 : 1;
    return ua < ub ? 1 : -1;
}

int __gedf2(double a, double b) {
    u64 ua = ((du){.d = a}).u;
    u64 ub = ((du){.d = b}).u;
    if (is_nan(ua) || is_nan(ub)) return -1;
    if (is_zero(ua) && is_zero(ub)) return 0;
    u64 sa = ua & SIGN_BIT;
    u64 sb = ub & SIGN_BIT;
    if (sa != sb) return sa ? -1 : 0;
    if (ua == ub) return 0;
    if (!sa) return ua >= ub ? 0 : -1;
    return ua <= ub ? 0 : -1;
}

int __ledf2(double a, double b) {
    u64 ua = ((du){.d = a}).u;
    u64 ub = ((du){.d = b}).u;
    if (is_nan(ua) || is_nan(ub)) return 1;
    if (is_zero(ua) && is_zero(ub)) return 0;
    u64 sa = ua & SIGN_BIT;
    u64 sb = ub & SIGN_BIT;
    if (sa != sb) return sa ? 0 : 1;
    if (ua == ub) return 0;
    if (!sa) return ua <= ub ? 0 : 1;
    return ua >= ub ? 0 : 1;
}

int __eqdf2(double a, double b) {
    u64 ua = ((du){.d = a}).u;
    u64 ub = ((du){.d = b}).u;
    if (is_nan(ua) || is_nan(ub)) return 1;
    if (is_zero(ua) && is_zero(ub)) return 0;
    return ua == ub ? 0 : 1;
}

int __nedf2(double a, double b) {
    return __eqdf2(a, b);
}

/* Integer to double conversions */
double __floatsidf(int a) {
    if (a == 0) return ((du){.u = 0}).d;
    u64 sign = a < 0 ? SIGN_BIT : 0;
    u64 val = a < 0 ? -(u64)(long long)a : (u64)a;
    int shift = 63 - __builtin_clzll(val);
    int exp = 1023 + shift;
    u64 mant = (val ^ (1ULL << shift)) << (52 - shift);
    return ((du){.u = sign | ((u64)exp << 52) | mant}).d;
}

double __floatdidf(long long a) {
    if (a == 0) return ((du){.u = 0}).d;
    u64 sign = a < 0 ? SIGN_BIT : 0;
    u64 val = a < 0 ? -(u64)a : (u64)a;
    int shift = 63 - __builtin_clzll(val);
    int exp = 1023 + shift;
    u64 mant = 0;
    if (shift <= 52) {
        mant = (val ^ (1ULL << shift)) << (52 - shift);
    } else {
        int excess = shift - 52;
        mant = (val ^ (1ULL << shift)) >> excess;
        u64 round_bit = (val >> (excess - 1)) & 1;
        u64 sticky = (val & ((1ULL << (excess - 1)) - 1)) != 0 ? 1 : 0;
        if (round_bit && (sticky || (mant & 1))) {
            mant++;
            if (mant == (1ULL << 52)) {
                exp++;
                mant = 0;
            }
        }
    }
    return ((du){.u = sign | ((u64)exp << 52) | (mant & MANT_MASK)}).d;
}

/* Double to integer conversions */
int __fixdfsi(double a) {
    u64 u = ((du){.d = a}).u;
    int sign = (u & SIGN_BIT) != 0;
    int exp = (int)((u & EXP_MASK) >> 52);
    if (exp == 0x7FF) return sign ? (int)0x80000000 : 0x7FFFFFFF;
    if (exp < 1023) return 0;
    int e = exp - 1023;
    u64 mant = (1ULL << 52) | (u & MANT_MASK);
    u64 val;
    if (e >= 52) {
        if (e - 52 >= 32) return sign ? (int)0x80000000 : 0x7FFFFFFF;
        val = mant << (e - 52);
    } else {
        val = mant >> (52 - e);
    }
    if (sign) {
        if (val >= 0x80000000ULL) return (int)0x80000000;
        return -(int)val;
    }
    if (val >= 0x7FFFFFFFULL) return 0x7FFFFFFF;
    return (int)val;
}

unsigned int __fixunsdfsi(double a) {
    u64 u = ((du){.d = a}).u;
    if (u & SIGN_BIT) return 0;
    int exp = (int)((u & EXP_MASK) >> 52);
    if (exp == 0x7FF) return 0xFFFFFFFFU;
    if (exp < 1023) return 0;
    int e = exp - 1023;
    u64 mant = (1ULL << 52) | (u & MANT_MASK);
    u64 val;
    if (e >= 52) {
        if (e - 52 >= 32) return 0xFFFFFFFFU;
        val = mant << (e - 52);
    } else {
        val = mant >> (52 - e);
    }
    if (val >= 0xFFFFFFFFULL) return 0xFFFFFFFFU;
    return (unsigned int)val;
}

long long __fixdfdi(double a) {
    u64 u = ((du){.d = a}).u;
    int sign = (u & SIGN_BIT) != 0;
    int exp = (int)((u & EXP_MASK) >> 52);
    if (exp == 0x7FF) return sign ? (long long)0x8000000000000000ULL : 0x7FFFFFFFFFFFFFFFLL;
    if (exp < 1023) return 0;
    int e = exp - 1023;
    u64 mant = (1ULL << 52) | (u & MANT_MASK);
    u64 val;
    if (e >= 52) {
        if (e - 52 >= 11) return sign ? (long long)0x8000000000000000ULL : 0x7FFFFFFFFFFFFFFFLL;
        val = mant << (e - 52);
    } else {
        val = mant >> (52 - e);
    }
    if (sign) {
        if (val >= 0x8000000000000000ULL) return (long long)0x8000000000000000ULL;
        return -(long long)val;
    }
    if (val >= (u64)0x7FFFFFFFFFFFFFFFLL) return 0x7FFFFFFFFFFFFFFFLL;
    return (long long)val;
}

/* Multiplication */
double __muldf3(double a, double b) {
    u64 ua = ((du){.d = a}).u;
    u64 ub = ((du){.d = b}).u;
    u64 sign = (ua ^ ub) & SIGN_BIT;
    int ea = (int)((ua & EXP_MASK) >> 52);
    int eb = (int)((ub & EXP_MASK) >> 52);
    u64 ma = ua & MANT_MASK;
    u64 mb = ub & MANT_MASK;

    if ((ea == 0x7FF && ma != 0) || (eb == 0x7FF && mb != 0)) return ((du){.u = CANON_QNAN}).d;
    if ((ea == 0x7FF && is_zero(ub)) || (eb == 0x7FF && is_zero(ua))) return ((du){.u = CANON_QNAN}).d;
    if (ea == 0x7FF || eb == 0x7FF) return ((du){.u = sign | EXP_MASK}).d;
    if (is_zero(ua) || is_zero(ub)) return ((du){.u = sign}).d;

    if (ea == 0) {
        int s = 63 - __builtin_clzll(ma);
        ma <<= (52 - s);
        ea = 1 - (52 - s);
    } else {
        ma |= (1ULL << 52);
    }

    if (eb == 0) {
        int s = 63 - __builtin_clzll(mb);
        mb <<= (52 - s);
        eb = 1 - (52 - s);
    } else {
        mb |= (1ULL << 52);
    }

    int exp = ea + eb - 1023;

    u32 a0 = (u32)ma, a1 = (u32)(ma >> 32);
    u32 b0 = (u32)mb, b1 = (u32)(mb >> 32);
    u64 p00 = (u64)a0 * b0;
    u64 p01 = (u64)a0 * b1;
    u64 p10 = (u64)a1 * b0;
    u64 p11 = (u64)a1 * b1;
    u64 mid = p01 + (p00 >> 32) + (u32)p10;
    u64 lo = (mid << 32) | (u32)p00;
    u64 hi = p11 + (mid >> 32) + (p10 >> 32);

    int shift;
    if ((hi >> 41) & 1) {
        exp++;
        shift = 53;
    } else {
        shift = 52;
    }

    if (exp <= 0) {
        shift += (1 - exp);
        exp = 0;
    }

    if (shift > 106) return ((du){.u = sign}).d;

    u64 mant = 0;
    u64 round_bit = 0;
    u64 sticky = 0;

    if (shift < 64) {
        mant = (hi << (64 - shift)) | (lo >> shift);
        round_bit = (lo >> (shift - 1)) & 1;
        sticky = (shift > 1 && (lo & ((1ULL << (shift - 1)) - 1)) != 0);
    } else {
        int s = shift - 64;
        mant = hi >> s;
        round_bit = s > 0 ? ((hi >> (s - 1)) & 1) : (lo >> 63);
        sticky = (lo != 0) || (s > 1 && (hi & ((1ULL << (s - 1)) - 1)) != 0);
    }

    if (round_bit && (sticky || (mant & 1))) {
        mant++;
        if (exp == 0 && (mant & (1ULL << 52))) {
            exp = 1;
        } else if (exp > 0 && (mant & (1ULL << 53))) {
            exp++;
            mant >>= 1;
        }
    }

    if (exp >= 2047) return ((du){.u = sign | EXP_MASK}).d;
    return ((du){.u = sign | ((u64)exp << 52) | (mant & MANT_MASK)}).d;
}

/* Division */
double __divdf3(double a, double b) {
    u64 ua = ((du){.d = a}).u;
    u64 ub = ((du){.d = b}).u;
    u64 sign = (ua ^ ub) & SIGN_BIT;
    int ea = (int)((ua & EXP_MASK) >> 52);
    int eb = (int)((ub & EXP_MASK) >> 52);
    u64 ma = ua & MANT_MASK;
    u64 mb = ub & MANT_MASK;

    if ((ea == 0x7FF && ma != 0) || (eb == 0x7FF && mb != 0)) return ((du){.u = CANON_QNAN}).d;
    if ((ea == 0x7FF && eb == 0x7FF) || (is_zero(ua) && is_zero(ub))) return ((du){.u = CANON_QNAN}).d;
    if (is_zero(ub) || ea == 0x7FF) return ((du){.u = sign | EXP_MASK}).d;
    if (is_zero(ua) || eb == 0x7FF) return ((du){.u = sign}).d;

    if (ea == 0) {
        int s = 63 - __builtin_clzll(ma);
        ma <<= (52 - s);
        ea = 1 - (52 - s);
    } else {
        ma |= (1ULL << 52);
    }

    if (eb == 0) {
        int s = 63 - __builtin_clzll(mb);
        mb <<= (52 - s);
        eb = 1 - (52 - s);
    } else {
        mb |= (1ULL << 52);
    }

    int exp = ea - eb + 1023;
    if (ma < mb) {
        ma <<= 1;
        exp--;
    }

    u64 q = 0;
    u64 rem = ma;
    for (int i = 0; i < 54; i++) {
        q <<= 1;
        if (rem >= mb) {
            rem -= mb;
            q |= 1;
        }
        rem <<= 1;
    }

    u64 sticky = rem != 0;

    if (exp <= 0) {
        int s = 1 - exp;
        exp = 0;
        if (s > 54) return ((du){.u = sign}).d;
        sticky = sticky || ((q & ((1ULL << s) - 1)) != 0);
        q >>= s;
    }

    u64 mant = q >> 1;
    u64 round_bit = q & 1;

    if (round_bit && (sticky || (mant & 1))) {
        mant++;
        if (exp == 0 && (mant & (1ULL << 52))) {
            exp = 1;
        } else if (exp > 0 && (mant & (1ULL << 53))) {
            exp++;
            mant >>= 1;
        }
    }

    if (exp >= 2047) return ((du){.u = sign | EXP_MASK}).d;
    return ((du){.u = sign | ((u64)exp << 52) | (mant & MANT_MASK)}).d;
}

/* Addition and Subtraction */
double __adddf3(double a, double b) {
    u64 ua = ((du){.d = a}).u;
    u64 ub = ((du){.d = b}).u;
    int ea = (int)((ua & EXP_MASK) >> 52);
    int eb = (int)((ub & EXP_MASK) >> 52);
    u64 ma = ua & MANT_MASK;
    u64 mb = ub & MANT_MASK;
    u64 sa = ua & SIGN_BIT;
    u64 sb = ub & SIGN_BIT;

    if ((ea == 0x7FF && ma != 0) || (eb == 0x7FF && mb != 0)) return ((du){.u = CANON_QNAN}).d;
    if (ea == 0x7FF && eb == 0x7FF) {
        if (sa != sb) return ((du){.u = CANON_QNAN}).d;
        return a;
    }
    if (ea == 0x7FF) return a;
    if (eb == 0x7FF) return b;
    if (is_zero(ua)) return is_zero(ub) ? ((du){.u = sa == sb ? sa : 0}).d : b;
    if (is_zero(ub)) return a;

    if (ea < eb || (ea == eb && ma < mb)) {
        u64 ut = ua; ua = ub; ub = ut;
        u64 st = sa; sa = sb; sb = st;
        int et = ea; ea = eb; eb = et;
        u64 mt = ma; ma = mb; mb = mt;
    }

    u64 res_sign = sa;
    ma = (ea == 0) ? (ma << 3) : ((ma | (1ULL << 52)) << 3);
    if (ea == 0) ea = 1;
    mb = (eb == 0) ? (mb << 3) : ((mb | (1ULL << 52)) << 3);
    if (eb == 0) eb = 1;

    int diff = ea - eb;
    if (diff > 0) {
        if (diff >= 60) {
            mb = 1;
        } else {
            u64 sticky = (mb & ((1ULL << diff) - 1)) != 0;
            mb = (mb >> diff) | sticky;
        }
    }

    u64 mant;
    if (sa == sb) {
        mant = ma + mb;
        if ((mant >> 56) & 1) {
            u64 sticky = mant & 1;
            mant = (mant >> 1) | sticky;
            ea++;
        }
    } else {
        mant = ma - mb;
        if (mant == 0) return ((du){.u = 0}).d;
        int s = 55 - (63 - __builtin_clzll(mant));
        if (s > 0) {
            if (ea > s) {
                mant <<= s;
                ea -= s;
            } else {
                mant <<= (ea - 1);
                ea = 0;
            }
        }
    }

    u64 round_b = (mant >> 2) & 1;
    u64 sticky = (mant & 3) != 0;
    u64 m = mant >> 3;
    if (round_b && (sticky || (m & 1))) {
        m++;
        if (m == (1ULL << 53)) {
            m >>= 1;
            ea++;
        }
    }

    if (ea >= 2047) return ((du){.u = res_sign | EXP_MASK}).d;
    if (ea == 0) return ((du){.u = res_sign | (m & MANT_MASK)}).d;
    return ((du){.u = res_sign | ((u64)ea << 52) | (m & MANT_MASK)}).d;
}

double __subdf3(double a, double b) {
    u64 ub = ((du){.d = b}).u;
    return __adddf3(a, ((du){.u = ub ^ SIGN_BIT}).d);
}
