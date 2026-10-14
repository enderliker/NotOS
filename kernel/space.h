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
    u32 entry,stack_low,stack_top,heap_end,initial_stack;
};
/* Destination must be zero initialized. Failures release every acquired frame. */
int space_create(struct user_space *space,const u8 *image,unsigned bytes);
int space_create_elf(struct user_space *space,const u8 *image,u32 bytes);
int space_copy_to_user(struct user_space *space,u32 address,const void *data,u32 bytes);
int space_arguments(struct user_space *space,const char *name,const char *argument);
/* Prepare aligned argc/argv; space-separated text has bounded length/count. */
int space_arguments_split(struct user_space *space,const char *name,const char *arguments);
/* Prepare aligned argc/argv/envp on the user stack (max 16 args, 16 env vars). */
int space_stack_build(struct user_space *space,const char *const *argv,u32 argc,
    const char *const *envp,u32 envc);
int space_destroy(struct user_space *space);
int space_range(const struct user_space *space,u32 address,u32 bytes,int writable);
/* Return old break on failure. These are NotOS APIs, not a Linux syscall ABI. */
u32 space_brk(struct user_space *space,u32 requested);
/* Anonymous private RW/NX only; no fixed-address or file mappings. */
u32 space_mmap(struct user_space *space,u32 bytes);
int space_munmap(struct user_space *space,u32 address,u32 bytes);
int space_copy_from_user(const struct user_space *,u32 address,void *data,u32 bytes);
#endif
