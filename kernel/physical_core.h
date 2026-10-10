#ifndef NOTOS_PHYSICAL_CORE_H
#define NOTOS_PHYSICAL_CORE_H
#include "base.h"
#define PAGE_SIZE 4096u
#define PHYSICAL_LIMIT (64u*1024u*1024u)
#define FRAME_COUNT (PHYSICAL_LIMIT/PAGE_SIZE)
struct e820_entry {unsigned long long base,size;u32 type,attributes;} __attribute__((packed));
struct frame_pool {u32 usable[FRAME_COUNT/32],free[FRAME_COUNT/32];u32 total,available;};
int frame_pool_init(struct frame_pool *pool,const struct e820_entry *entries,unsigned count);
int frame_reserve(struct frame_pool *pool,u32 base,u32 bytes);
u32 frame_take(struct frame_pool *pool);
int frame_release(struct frame_pool *pool,u32 address);
int frame_usable(const struct frame_pool *pool,u32 address);
int frame_owned(const struct frame_pool *pool,u32 address);
#endif
