/* QEMU firmware seed from the host CSPRNG; no PIT jitter or deterministic fallback. */
#include "platform_tls.h"
#include <bearssl.h>
static br_hmac_drbg_context generator;
static int ready;
void secure_clear(void *buffer,u32 bytes) {volatile u8 *p=buffer;while (bytes--) *p++=0;}
static u32 fw32(void) {
    u32 value=0;for (unsigned i=0;i<4;i++) value=(value<<8)|inb(0x511);return value;
}
static int firmware_seed(u8 seed[64]) {
    outw(0x510,0);
    static const char magic[]="QEMU";
    for (unsigned i=0;i<4;i++) if (inb(0x511)!=(u8)magic[i]) return 0;
    outw(0x510,0x19);u32 count=fw32();
    if (count>256) return 0;
    for (unsigned i=0;i<count;i++) {
        u32 bytes=fw32();u16 selector=(u16)((u16)inb(0x511)<<8);selector|=inb(0x511);
        (void)inb(0x511);(void)inb(0x511);char name[56];
        for (unsigned j=0;j<56;j++) name[j]=(char)inb(0x511);
        if (name[55] || !equal(name,"opt/notos/entropy")) continue;
        if (bytes!=64) return 0;
        outw(0x510,selector);for (unsigned j=0;j<64;j++) seed[j]=inb(0x511);
        u8 any=0;for (unsigned j=0;j<64;j++) any|=seed[j];return any!=0;
    }
    return 0;
}
int entropy_init(void) {
    u8 seed[64];u32 flags=irq_save();int found=firmware_seed(seed);irq_restore(flags);
    ready=0;
    if (found) {br_hmac_drbg_init(&generator,&br_sha256_vtable,seed,sizeof(seed));ready=1;}
    secure_clear(seed,sizeof(seed));return ready;
}
int entropy_get(void *buffer,u32 bytes) {
    if (!ready || !buffer || !bytes || bytes>64) return 0;
    br_hmac_drbg_generate(&generator,buffer,bytes);return 1;
}
int entropy_ready(void) {return ready;}

/* BearSSL must never silently seek a host OS RNG or CPU extension. */
br_prng_seeder br_prng_seeder_system(const char **name) {
    if (name) *name="none";
    return 0;
}
