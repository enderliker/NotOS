#include "certificate_policy.h"
struct slice {const u8 *data;unsigned bytes;};
static int take(struct slice *input,unsigned *tag,struct slice *value) {
    if (input->bytes<2) return 0;
    unsigned length=input->data[1],header=2;*tag=input->data[0];
    if (length&128) {
        unsigned count=length&127;
        if (!count || count>4 || input->bytes<2+count) return 0;
        length=0;
        for (unsigned i=0;i<count;i++) length=(length<<8)|input->data[header++];
    }
    if (length>input->bytes-header) return 0;
    *value=(struct slice){input->data+header,length};
    input->data+=header+length;input->bytes-=header+length;return 1;
}
static int tagged(struct slice *input,unsigned expected,struct slice *value) {
    unsigned tag;return take(input,&tag,value) && tag==expected;
}
static int oid(struct slice value,const u8 *bytes,unsigned length) {
    if (value.bytes!=length) return 0;
    for (unsigned i=0;i<length;i++) if (value.data[i]!=bytes[i]) return 0;
    return 1;
}
static int allowed_key(struct slice spki) {
    static const u8 rsa[]={0x2a,0x86,0x48,0x86,0xf7,0x0d,1,1,1};
    static const u8 ec[]={0x2a,0x86,0x48,0xce,0x3d,2,1};
    static const u8 p256[]={0x2a,0x86,0x48,0xce,0x3d,3,1,7};
    static const u8 p384[]={0x2b,0x81,4,0,0x22},p521[]={0x2b,0x81,4,0,0x23};
    struct slice algorithm,id,bits,value;
    if (!tagged(&spki,0x30,&algorithm) || !tagged(&algorithm,6,&id)
        || !tagged(&spki,3,&bits) || spki.bytes || !bits.bytes || bits.data[0]) return 0;
    if (oid(id,ec,sizeof(ec))) {
        return tagged(&algorithm,6,&value) && !algorithm.bytes
            && (oid(value,p256,sizeof(p256)) || oid(value,p384,sizeof(p384)) || oid(value,p521,sizeof(p521)));
    }
    if (!oid(id,rsa,sizeof(rsa))) return 0;
    bits.data++;bits.bytes--;
    struct slice public_key,modulus;
    if (!tagged(&bits,0x30,&public_key) || bits.bytes || !tagged(&public_key,2,&modulus)) return 0;
    while (modulus.bytes && !modulus.data[0]) {modulus.data++;modulus.bytes--;}
    /* Byte-count-only checks would also admit 2041..2047-bit keys. */
    return modulus.bytes>256 || (modulus.bytes==256 && (modulus.data[0]&0x80));
}

int certificate_policy_role(const u8 *der,unsigned bytes,int require_dns_san) {
    static const u8 san_oid[]={0x55,0x1d,0x11}, eku_oid[]={0x55,0x1d,0x25};
    static const u8 server_auth[]={0x2b,6,1,5,5,7,3,1}, any_eku[]={0x55,0x1d,0x25,0};
    if (!der || !bytes || bytes>16384) return 0;
    struct slice input={der,bytes},certificate,tbs,value;
    if (!tagged(&input,0x30,&certificate) || input.bytes || !tagged(&certificate,0x30,&tbs)) return 0;
    if (tbs.bytes && tbs.data[0]==0xa0 && !tagged(&tbs,0xa0,&value)) return 0;
    unsigned tag;
    for (unsigned i=0;i<6;i++) if (!take(&tbs,&tag,&value)) return 0;
    if (tag!=0x30 || !allowed_key(value)) return 0;
    int dns=0,seen_san=0,seen_eku=0;
    while (tbs.bytes) {
        if (!take(&tbs,&tag,&value)) return 0;
        if (tag!=0xa3) continue;
        struct slice extensions;
        if (!tagged(&value,0x30,&extensions) || value.bytes) return 0;
        unsigned count=0;
        while (extensions.bytes) {
            struct slice extension,id,encoded;
            if (++count>64 || !tagged(&extensions,0x30,&extension) || !tagged(&extension,6,&id)) return 0;
            if (extension.bytes && extension.data[0]==1 && !tagged(&extension,1,&value)) return 0;
            if (!tagged(&extension,4,&encoded) || extension.bytes) return 0;
            if (oid(id,san_oid,sizeof(san_oid))) {
                if (seen_san++) return 0;
                struct slice names;
                if (!tagged(&encoded,0x30,&names) || encoded.bytes) return 0;
                while (names.bytes) {
                    if (!take(&names,&tag,&value)) return 0;
                    if (tag==0x82 && value.bytes) dns=1;
                }
            } else if (oid(id,eku_oid,sizeof(eku_oid))) {
                if (seen_eku++) return 0;
                struct slice purposes;int allowed=0;
                if (!tagged(&encoded,0x30,&purposes) || encoded.bytes) return 0;
                while (purposes.bytes) {
                    if (!tagged(&purposes,6,&value)) return 0;
                    if (oid(value,server_auth,sizeof(server_auth)) || oid(value,any_eku,sizeof(any_eku))) allowed=1;
                }
                if (!allowed) return 0;
            }
        }
    }
    return !require_dns_san || (seen_san && dns);
}

int certificate_policy(const u8 *der,unsigned bytes) {return certificate_policy_role(der,bytes,1);}
