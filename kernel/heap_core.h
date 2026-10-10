#ifndef NOTOS_HEAP_CORE_H
#define NOTOS_HEAP_CORE_H
#include "base.h"
struct heap_block;
struct heap_pool {u8 *base;u32 bytes;struct heap_block *head;};
struct heap_info {u32 free_bytes,used_bytes,allocations,largest_free;};
int heap_pool_init(struct heap_pool *pool,void *arena,u32 bytes);
int heap_valid(const struct heap_pool *pool);
void *heap_take(struct heap_pool *pool,u32 bytes);
void *heap_zero(struct heap_pool *pool,u32 count,u32 bytes);
int heap_release(struct heap_pool *pool,void *pointer);
int heap_inspect(const struct heap_pool *pool,struct heap_info *info);
#endif
