#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "../kernel/certificate_policy.h"
static unsigned read_der(const char *name,unsigned char *buffer) {
    FILE *file=fopen(name,"rb");assert(file);
    unsigned bytes=(unsigned)fread(buffer,1,16384,file);assert(!ferror(file));fclose(file);return bytes;
}
int main(void) {
    unsigned char buffer[16384],scratch[16384];
    unsigned bytes=read_der("build/tls-fixtures/valid.der",buffer);
    assert(certificate_policy(buffer,bytes));
    for (unsigned n=0;n<bytes;n++) assert(!certificate_policy(buffer,n));
    unsigned random=7;
    for (unsigned i=0;i<10000;i++) {
        memcpy(scratch,buffer,bytes);
        random=random*1664525u+1013904223u;scratch[random%bytes]^=(unsigned char)(random>>24);
        (void)certificate_policy(scratch,bytes);
    }
    bytes=read_der("build/tls-fixtures/no-san.der",buffer);assert(!certificate_policy(buffer,bytes));
    bytes=read_der("build/tls-fixtures/client-only.der",buffer);assert(!certificate_policy(buffer,bytes));
    bytes=read_der("build/tls-fixtures/weak-edge.der",buffer);assert(!certificate_policy(buffer,bytes));
    assert(!certificate_policy(0,100) && !certificate_policy(buffer,16385));
    puts("PASS: DNS SAN/EKU DER policy, every truncated prefix and 10000 mutated inputs (ASAN/UBSAN)");
}
