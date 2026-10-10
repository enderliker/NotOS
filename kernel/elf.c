/* Strict static ELF32/i386: no interpreter, dynamic relocations or TLS segments. */
#include "elf.h"
static u16 read16(const u8 *p) {return (u16)(p[0]|(u16)p[1]<<8);}
static u32 read32(const u8 *p) {return (u32)p[0]|(u32)p[1]<<8|(u32)p[2]<<16|(u32)p[3]<<24;}
static int contained(u32 offset,u32 length,u32 size) {return offset<=size && length<=size-offset;}
int elf_validate(const u8 *image,u32 bytes,struct elf_plan *output) {
    if (!image || !output || bytes<52 || bytes>2u*1024u*1024u) return 0;
    if (image[0]!=0x7f || image[1]!='E' || image[2]!='L' || image[3]!='F'
        || image[4]!=1 || image[5]!=1 || image[6]!=1 || image[7]!=0 || image[8]!=0
        || read16(image+16)!=2 || read16(image+18)!=3 || read32(image+20)!=1
        || read32(image+36)!=0 || read16(image+40)!=52 || read16(image+42)!=32) return 0;
    u32 offset=read32(image+28),count=read16(image+44);
    if (!count || count>ELF_SEGMENTS_MAX || offset<52 || !contained(offset,count*32,bytes)) return 0;
    struct elf_plan plan={0};
    plan.entry=read32(image+24);
    int executable_entry=0;
    for (u32 i=0;i<count;i++) {
        const u8 *header=image+offset+i*32;
        u32 type=read32(header);
        if (type==2 || type==3 || type==7) return 0;
        if (type==0x6474e551u) {
            if (read32(header+24)&1) return 0; /* Executable GNU_STACK. */
            continue;
        }
        if (type!=1) continue;
        struct elf_segment segment={read32(header+4),read32(header+8),
            read32(header+16),read32(header+20),read32(header+24)};
        u32 alignment=read32(header+28);
        if (segment.file_bytes>segment.memory_bytes || !contained(segment.offset,segment.file_bytes,bytes)
            || (segment.flags&~7u) || !(segment.flags&4) || (segment.flags&3)==3
            || (alignment>1 && ((alignment&(alignment-1)) || alignment>0x200000u
                || (segment.address&(alignment-1))!=(segment.offset&(alignment-1))))) return 0;
        if (!segment.memory_bytes) continue;
        if (segment.address<ELF_IMAGE_BASE || segment.address>=ELF_IMAGE_LIMIT
            || segment.memory_bytes>ELF_IMAGE_LIMIT-segment.address) return 0;
        u32 low=segment.address&~4095u;
        u32 high=(segment.address+segment.memory_bytes+4095u)&~4095u;
        for (u32 j=0;j<plan.count;j++) {
            const struct elf_segment *other=&plan.segments[j];
            u32 other_low=other->address&~4095u;
            u32 other_high=(other->address+other->memory_bytes+4095u)&~4095u;
            if (low<other_high && other_low<high) return 0; /* No shared-page permission merging. */
        }
        if ((segment.flags&1) && plan.entry>=segment.address && plan.entry-segment.address<segment.file_bytes)
            executable_entry=1;
        plan.segments[plan.count++]=segment;
    }
    if (!plan.count || !executable_entry) return 0;
    *output=plan;
    return 1;
}
