#ifndef NOTOS_USER_MEMORY_H
#define NOTOS_USER_MEMORY_H
#include "base.h"
#include "paging_entries.h"
#define USER_BASE 0x08000000u
#define USER_SIZE 0x00400000u
int user_memory_check(const struct page_entry *table,u32 pde,u32 address,u32 bytes,int writable);
#endif
