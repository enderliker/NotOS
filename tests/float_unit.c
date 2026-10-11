/* Differential unit test for user/float_runtime.c against host hardware IEEE 754. */
#include <stdio.h>
#include <stdlib.h>
#include <stdint.h>
#include <stdbool.h>
#include <math.h>
#include <float.h>
#include <limits.h>
#include <assert.h>

typedef uint64_t u64;
typedef uint32_t u32;

typedef union {
    double d;
    u64 u;
} du;

/* Prototypes mapped with FLOAT_TEST_PREFIX */
extern double test_floatsidf(int);
extern double test_floatdidf(long long);
extern int test_fixdfsi(double);
extern unsigned int test_fixunsdfsi(double);
extern long long test_fixdfdi(double);
extern double test_muldf3(double, double);
extern double test_divdf3(double, double);
extern double test_adddf3(double, double);
extern double test_subdf3(double, double);
extern int test_gtdf2(double, double);
extern int test_ltdf2(double, double);
extern int test_gedf2(double, double);
extern int test_ledf2(double, double);
extern int test_eqdf2(double, double);
extern int test_nedf2(double, double);

static inline u64 to_u(double d) {
    return ((du){.d = d}).u;
}

static inline double to_d(u64 u) {
    return ((du){.u = u}).d;
}

static inline bool check_eq(double actual, double expected) {
    u64 ua = to_u(actual);
    u64 ue = to_u(expected);
    if (isnan(actual) && isnan(expected)) return true;
    return ua == ue;
}

/* Fast PRNG: xorshift64* */
static u64 prng_state = 0x853c49e6748fea9bULL;
static inline u64 next_u64(void) {
    u64 x = prng_state;
    x ^= x >> 12;
    x ^= x << 25;
    x ^= x >> 27;
    prng_state = x;
    return x * 0x2545F4914F6CDD1DULL;
}

static void test_known_cases(void) {
    /* Basic arithmetic */
    assert(test_muldf3(3.0, 4.0) == 12.0);
    assert(test_divdf3(12.0, 4.0) == 3.0);
    assert(test_adddf3(5.5, 4.5) == 10.0);
    assert(test_subdf3(10.0, 3.5) == 6.5);

    /* Conversions */
    assert(test_floatsidf(42) == 42.0);
    assert(test_floatsidf(-123) == -123.0);
    assert(test_floatdidf(1000000000000LL) == 1000000000000.0);
    assert(test_floatdidf(-9876543210123LL) == -9876543210123.0);
    assert(test_fixdfsi(3.7) == 3);
    assert(test_fixdfsi(-3.7) == -3);
    assert(test_fixunsdfsi(42.9) == 42);
    assert(test_fixdfdi(12345678901234.5) == 12345678901234LL);

    /* Comparisons */
    assert(test_gtdf2(5.0, 3.0) > 0);
    assert(test_gtdf2(3.0, 5.0) <= 0);
    assert(test_gtdf2(3.0, 3.0) <= 0);
    assert(test_ltdf2(3.0, 5.0) < 0);
    assert(test_ltdf2(5.0, 3.0) >= 0);
    assert(test_ltdf2(3.0, 3.0) >= 0);
    assert(test_gedf2(5.0, 3.0) >= 0);
    assert(test_gedf2(3.0, 3.0) >= 0);
    assert(test_gedf2(3.0, 5.0) < 0);
    assert(test_ledf2(3.0, 5.0) <= 0);
    assert(test_ledf2(3.0, 3.0) <= 0);
    assert(test_ledf2(5.0, 3.0) > 0);
    assert(test_eqdf2(3.0, 3.0) == 0);
    assert(test_eqdf2(3.0, 5.0) != 0);
    assert(test_nedf2(3.0, 5.0) != 0);
    assert(test_nedf2(3.0, 3.0) == 0);

    /* NaN comparisons */
    double nan_val = NAN;
    assert(test_gtdf2(nan_val, 1.0) <= 0);
    assert(test_gtdf2(1.0, nan_val) <= 0);
    assert(test_ltdf2(nan_val, 1.0) >= 0);
    assert(test_ltdf2(1.0, nan_val) >= 0);
    assert(test_gedf2(nan_val, 1.0) < 0);
    assert(test_ledf2(nan_val, 1.0) > 0);
    assert(test_eqdf2(nan_val, 1.0) != 0);
    assert(test_nedf2(nan_val, 1.0) != 0);

    /* Edge cases: 0 / -0 */
    assert(test_eqdf2(0.0, -0.0) == 0);
    assert(test_gtdf2(0.0, -0.0) <= 0);
    assert(test_ltdf2(0.0, -0.0) >= 0);
    assert(check_eq(test_divdf3(1.0, 0.0), INFINITY));
    assert(check_eq(test_divdf3(-1.0, 0.0), -INFINITY));
    assert(check_eq(test_divdf3(0.0, 0.0), NAN));
    assert(check_eq(test_muldf3(INFINITY, 0.0), NAN));
    assert(check_eq(test_muldf3(-INFINITY, 0.0), NAN));

    /* Edge conversions */
    assert(test_fixdfsi(DBL_MAX) == INT_MAX);
    assert(test_fixdfsi(-DBL_MAX) == INT_MIN);
    assert(test_fixunsdfsi(DBL_MAX) == UINT_MAX);
    assert(test_fixunsdfsi(-1.0) == 0);
    assert(test_fixdfdi(DBL_MAX) == LLONG_MAX);
    assert(test_fixdfdi(-DBL_MAX) == LLONG_MIN);
}

static void test_differential_fuzzing(void) {
    const int count = 1000000;
    for (int i = 0; i < count; i++) {
        u64 ua = next_u64();
        u64 ub = next_u64();
        double a = to_d(ua);
        double b = to_d(ub);

        /* Compare comparisons */
        if (!isnan(a) && !isnan(b)) {
            int gt_soft = test_gtdf2(a, b) > 0;
            int gt_host = a > b;
            assert(gt_soft == gt_host);

            int lt_soft = test_ltdf2(a, b) < 0;
            int lt_host = a < b;
            assert(lt_soft == lt_host);

            int ge_soft = test_gedf2(a, b) >= 0;
            int ge_host = a >= b;
            assert(ge_soft == ge_host);

            int le_soft = test_ledf2(a, b) <= 0;
            int le_host = a <= b;
            assert(le_soft == le_host);

            int eq_soft = test_eqdf2(a, b) == 0;
            int eq_host = a == b;
            assert(eq_soft == eq_host);
        }

        if (isfinite(a) && isfinite(b)) {
            assert(check_eq(test_adddf3(a, b), a + b));
            assert(check_eq(test_subdf3(a, b), a - b));
            /* Multiplication */
            double mul_host = a * b;
            double mul_soft = test_muldf3(a, b);
            if (!check_eq(mul_soft, mul_host)) {
                fprintf(stderr, "MUL mismatch at %d: a=%.17g (0x%016llx) b=%.17g (0x%016llx)\n"
                                "  soft=%.17g (0x%016llx)\n"
                                "  host=%.17g (0x%016llx)\n",
                        i, a, (unsigned long long)ua, b, (unsigned long long)ub,
                        mul_soft, (unsigned long long)to_u(mul_soft),
                        mul_host, (unsigned long long)to_u(mul_host));
                abort();
            }

            /* Division */
            double div_host = a / b;
            double div_soft = test_divdf3(a, b);
            if (!check_eq(div_soft, div_host)) {
                fprintf(stderr, "DIV mismatch at %d: a=%.17g (0x%016llx) b=%.17g (0x%016llx)\n"
                                "  soft=%.17g (0x%016llx)\n"
                                "  host=%.17g (0x%016llx)\n",
                        i, a, (unsigned long long)ua, b, (unsigned long long)ub,
                        div_soft, (unsigned long long)to_u(div_soft),
                        div_host, (unsigned long long)to_u(div_host));
                abort();
            }
        }

        /* Integer conversions */
        int int_val = (int)ua;
        assert(test_floatsidf(int_val) == (double)int_val);
        long long ll_val = (long long)ua;
        assert(check_eq(test_floatdidf(ll_val), (double)ll_val));
    }
}

int main(void) {
    test_known_cases();
    printf("PASS: float_runtime known cases, edge cases and conversions\n");
    test_differential_fuzzing();
    printf("PASS: float_runtime differential fuzzing 1,000,000 random pairs against host hardware\n");
    return 0;
}
