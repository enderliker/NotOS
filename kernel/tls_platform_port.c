/* Small kernel crypto adapter, not a userspace libc or curl compatibility layer. */
#include "tls_platform_port.h"
#include "platform_tls.h"
#include "heap.h"
#include "clock.h"
#include <mbedtls/platform_util.h>
#include <psa/crypto.h>
#include <time.h>
void *notos_tls_calloc(size_t count,size_t bytes) {return kcalloc(count,bytes);}
void notos_tls_free(void *pointer) {if (pointer) (void)kfree(pointer);}
int notos_tls_snprintf(char *buffer,size_t bytes,const char *format,...) {
    (void)format;if (bytes && buffer) buffer[0]=0;return -1;
}
int notos_tls_printf(const char *format,...) {(void)format;return -1;}
void notos_tls_assert(const char *expression) {
    console_puts("TLS INTERNAL ASSERT: ");console_puts(expression);console_putc('\n');
    for (;;) __asm__ volatile("cli; hlt");
}
long long notos_tls_time(long long *output) {
    struct utc_time utc;long long value=-1;
    if (rtc_utc(&utc) && utc.days>=719528u)
        value=(long long)(utc.days-719528u)*86400+utc.seconds;
    if (output) *output=value;
    return value;
}
mbedtls_ms_time_t mbedtls_ms_time(void) {return clock_ms();}
struct tm *mbedtls_platform_gmtime_r(const mbedtls_time_t *time,struct tm *out) {
    static const unsigned lengths[]={31,28,31,30,31,30,31,31,30,31,30,31};
    if (!time || !out || *time<0 || *time>4102444799LL) return 0;
    u32 seconds=(u32)*time,days=seconds/86400;
    out->tm_sec=seconds%60;out->tm_min=seconds/60%60;out->tm_hour=seconds/3600%24;
    out->tm_wday=(int)((days+4)%7);unsigned year=1970;
    for (;;) {
        unsigned span=365+(year%4==0 && (year%100!=0 || year%400==0));
        if (days<span) break;
        days-=span;year++;
    }
    out->tm_year=(int)year-1900;out->tm_yday=(int)days;unsigned month=0;
    for (;;) {
        unsigned span=lengths[month]+(month==1 && year%4==0 && (year%100!=0 || year%400==0));
        if (days<span) break;
        days-=span;month++;
    }
    out->tm_mon=(int)month;out->tm_mday=(int)days+1;out->tm_isdst=0;return out;
}
int notos_tls_random(void *context,unsigned char *output,size_t bytes) {
    (void)context;
    while (bytes) {unsigned n=bytes>64?64:(unsigned)bytes;
        if (!entropy_get(output,n)) return -1;
        output+=n;bytes-=n;}
    return 0;
}
psa_status_t mbedtls_psa_external_get_random(mbedtls_psa_external_random_context_t *context,
    uint8_t *output,size_t bytes,size_t *length) {
    *length=0;if (notos_tls_random(context,output,bytes)) return PSA_ERROR_INSUFFICIENT_ENTROPY;
    *length=bytes;return PSA_SUCCESS;
}
