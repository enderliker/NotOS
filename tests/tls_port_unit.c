#define _POSIX_C_SOURCE 200809L
#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <mbedtls/platform_util.h>
#include "../kernel/platform_tls.h"
#include "../kernel/tls_platform_port.h"
static int available=1;
static unsigned random_calls;
void *kcalloc(u32 n,u32 bytes) {return calloc(n,bytes);}
int kfree(void *p) {free(p);return 1;}
void console_puts(const char *text) {(void)text;}
void console_putc(char ch) {(void)ch;}
unsigned clock_ms(void) {return 1234;}
int rtc_utc(struct utc_time *out) {out->days=730485;out->seconds=0;return available;}
int entropy_get(void *buffer,u32 bytes) {
    assert(bytes && bytes<=64);if (!available) return 0;
    memset(buffer,0x39,bytes);random_calls++;return 1;
}
extern int notos_tls_random(void *context,unsigned char *output,size_t bytes);
int main(void) {
    long long value;assert(notos_tls_time(&value)==946684800 && value==946684800);
    available=0;assert(notos_tls_time(0)==-1);available=1;
    assert(mbedtls_ms_time()==1234);
    long long epochs[]={0,951782400,2147483648LL,4102444799LL};
    for (unsigned i=0;i<10004;i++) {
        long long epoch=i<4?epochs[i]:(long long)((unsigned long long)i*399733ull%4102444800ull);
        struct tm actual;time_t native=(time_t)epoch;struct tm reference;
        assert(gmtime_r(&native,&reference));assert(mbedtls_platform_gmtime_r(&epoch,&actual));
        assert(actual.tm_year==reference.tm_year && actual.tm_mon==reference.tm_mon);
        assert(actual.tm_mday==reference.tm_mday && actual.tm_yday==reference.tm_yday);
        assert(actual.tm_wday==reference.tm_wday && actual.tm_hour==reference.tm_hour);
        assert(actual.tm_min==reference.tm_min && actual.tm_sec==reference.tm_sec);
    }
    struct tm tm;value=-1;assert(!mbedtls_platform_gmtime_r(&value,&tm));
    value=4102444800LL;assert(!mbedtls_platform_gmtime_r(&value,&tm));
    unsigned char bytes[257];assert(!notos_tls_random(0,bytes,sizeof(bytes)) && random_calls==5);
    for (unsigned i=0;i<sizeof(bytes);i++) assert(bytes[i]==0x39);
    available=0;assert(notos_tls_random(0,bytes,1));
    void *p=notos_tls_calloc(4,16);assert(p);notos_tls_free(p);notos_tls_free(0);
    puts("PASS: TLS UTC epoch/leap/2038/2099, 10004 calendar comparisons and RNG fail-closed");
}
