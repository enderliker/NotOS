#ifndef NOTOS_ELF_H
#define NOTOS_ELF_H
#include "base.h"
#define ELF_SEGMENTS_MAX 16u
#define ELF_IMAGE_BASE 0x08000000u
#define ELF_IMAGE_LIMIT 0x08400000u
struct elf_segment {u32 offset,address,file_bytes,memory_bytes,flags;};
struct elf_plan {u32 entry,count;struct elf_segment segments[ELF_SEGMENTS_MAX];};
/* Pure validation. Output is unchanged on failure; no allocation or mapping. */
int elf_validate(const u8 *image,u32 bytes,struct elf_plan *output);
#endif
