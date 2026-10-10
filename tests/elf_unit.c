#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "../kernel/elf.h"
static u8 original[65536],candidate[65536];
static u32 read32(const u8 *p) {return p[0]|(u32)p[1]<<8|(u32)p[2]<<16|(u32)p[3]<<24;}
static void write32(u8 *p,u32 n) {for (unsigned i=0;i<4;i++) p[i]=(u8)(n>>(8*i));}
static void reject(u32 bytes,u32 offset,u32 value) {
    memcpy(candidate,original,bytes);write32(candidate+offset,value);
    struct elf_plan output={0};output.count=0x12345678;
    struct elf_plan old=output;
    assert(!elf_validate(candidate,bytes,&output));
    assert(!memcmp(&output,&old,sizeof(old)));
}
int main(void) {
    FILE *file=fopen("build/elf-demo.bin","rb");assert(file);
    u32 bytes=(u32)fread(original,1,sizeof(original),file);assert(feof(file));fclose(file);
    struct elf_plan plan;assert(elf_validate(original,bytes,&plan));
    assert(plan.count>=2 && plan.entry==ELF_IMAGE_BASE);
    u32 ph=read32(original+28);
    reject(bytes,0,0);reject(bytes,4,0x00010102);reject(bytes,16,0x00030003);
    reject(bytes,20,2);reject(bytes,28,0xfffffff0);reject(bytes,36,1);reject(bytes,40,0);
    reject(bytes,44,17);reject(bytes,24,ELF_IMAGE_LIMIT);
    reject(bytes,ph,2);reject(bytes,ph+8,0x10000);reject(bytes,ph+8,ELF_IMAGE_LIMIT);
    reject(bytes,ph+4,0xfffffff0);reject(bytes,ph+16,0xffffffff);reject(bytes,ph+20,0xffffffff);
    reject(bytes,ph+24,7);reject(bytes,ph+24,0);reject(bytes,ph+28,3);
    memcpy(candidate,original,bytes);memcpy(candidate+ph+32,candidate+ph,32);
    assert(!elf_validate(candidate,bytes,&plan));
    assert(!elf_validate(0,bytes,&plan));assert(!elf_validate(original,bytes,0));
    for (u32 n=0;n<bytes;n++) {
        struct elf_plan output={0};output.count=0x12345678;
        struct elf_plan old=output;
        if (!elf_validate(original,n,&output)) assert(!memcmp(&output,&old,sizeof(old)));
    }
    u32 random=3;
    for (unsigned i=0;i<50000;i++) {
        memcpy(candidate,original,bytes);
        for (unsigned j=0;j<3;j++) {
            random=random*1664525u+1013904223u;
            candidate[random%bytes]^=(u8)(random>>24);
        }
        struct elf_plan output;
        if (elf_validate(candidate,bytes,&output)) {
            assert(output.count && output.count<=ELF_SEGMENTS_MAX);
            for (u32 n=0;n<output.count;n++) {
                const struct elf_segment *s=&output.segments[n];
                assert(s->file_bytes<=s->memory_bytes && s->address>=ELF_IMAGE_BASE);
                assert(s->memory_bytes<=ELF_IMAGE_LIMIT-s->address && (s->flags&3)!=3);
            }
        }
    }
    puts("PASS: ELF32 bounds/type/entry/W^X/overlap/alignment; all prefixes and 50000 mutations");
}
