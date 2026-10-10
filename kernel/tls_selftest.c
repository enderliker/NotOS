#include "tls_profile.h"
#include "platform_tls.h"
#include "heap.h"
int tls_selftest(void) {
    static const u8 expected[]={0xba,0x78,0x16,0xbf,0x8f,0x01,0xcf,0xea,
        0x41,0x41,0x40,0xde,0x5d,0xae,0x22,0x23,0xb0,0x03,0x61,0xa3,
        0x96,0x17,0x7a,0x9c,0xb4,0x10,0xff,0x61,0xf2,0x00,0x15,0xad};
    br_sha256_context hash;u8 result[32];br_sha256_init(&hash);
    br_sha256_update(&hash,"abc",3);br_sha256_out(&hash,result);
    for (unsigned i=0;i<32;i++) if (result[i]!=expected[i]) return 0;
    struct {br_ssl_client_context client;br_x509_minimal_context validator;} *contexts=kcalloc(1,sizeof(*contexts));
    if (!contexts) return 0;
    tls_profile(&contexts->client,&contexts->validator,0,0);
    int ok=contexts->client.eng.version_min==BR_TLS12 && contexts->client.eng.version_max==BR_TLS12;
    secure_clear(contexts,sizeof(*contexts));if (!kfree(contexts)) ok=0;
    struct utc_time time;if (!rtc_utc(&time)) ok=0;
    return ok;
}
