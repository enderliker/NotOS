#include "tls_profile.h"
#include "platform_tls.h"
#include "heap.h"
#include <bearssl.h>
int tls_selftest(void) {
    static const u8 expected[]={0xba,0x78,0x16,0xbf,0x8f,0x01,0xcf,0xea,
        0x41,0x41,0x40,0xde,0x5d,0xae,0x22,0x23,0xb0,0x03,0x61,0xa3,
        0x96,0x17,0x7a,0x9c,0xb4,0x10,0xff,0x61,0xf2,0x00,0x15,0xad};
    br_sha256_context hash;u8 result[32];br_sha256_init(&hash);
    br_sha256_update(&hash,"abc",3);br_sha256_out(&hash,result);
    for (unsigned i=0;i<32;i++) if (result[i]!=expected[i]) return 0;
    mbedtls_ssl_config *config=kcalloc(1,sizeof(*config));
    if (!config) return 0;
    mbedtls_ssl_config_init(config);
    int ok=!tls_profile(config) && config->MBEDTLS_PRIVATE(min_tls_version)==MBEDTLS_SSL_VERSION_TLS1_2
        && config->MBEDTLS_PRIVATE(max_tls_version)==MBEDTLS_SSL_VERSION_TLS1_3;
    mbedtls_ssl_config_free(config);
    secure_clear(config,sizeof(*config));if (!kfree(config)) ok=0;
    struct utc_time time;if (!rtc_utc(&time)) ok=0;
    return ok;
}
