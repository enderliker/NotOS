#ifndef NOTOS_SPACE_H
#define NOTOS_SPACE_H
#include "user_memory.h"
#define USER_HEAP_BASE 0x08400000u
#define USER_HEAP_LIMIT 0x08800000u
#define USER_ANON_BASE 0x08800000u
#define USER_ANON_LIMIT 0x08c00000u
#define USER_STACK_BASE 0x08ff0000u
#define USER_STACK_LIMIT 0x09000000u
struct user_space {
    u32 root,directory,table,code,data,stack;
    u32 entry,stack_low,stack_top,heap_end;
};
/* Destination must be zero initialized. Failures release every acquired frame. */
int space_create(struct user_space *space,const u8 *image,unsigned bytes);
int space_create_elf(struct user_space *space,const u8 *image,u32 bytes);
int space_destroy(struct user_space *space);
int space_range(const struct user_space *space,u32 address,u32 bytes,int writable);
/* Return old break on failure. These are NotOS APIs, not a Linux syscall ABI. */
u32 space_brk(struct user_space *space,u32 requested);
/* Anonymous private RW/NX only; no fixed-address or file mappings. */
u32 space_mmap(struct user_space *space,u32 bytes);
int space_munmap(struct user_space *space,u32 address,u32 bytes);
#endif
