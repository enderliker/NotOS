#ifndef NOTOS_PAGING_ENTRIES_H
#define NOTOS_PAGING_ENTRIES_H
#include "base.h"
struct page_entry {u32 low,high;};
#define PAGE_NX 0x80000000u
static inline void page_entry_set(struct page_entry *entry,u32 physical,u32 flags,int executable) {
    entry->low=0;
    __asm__ volatile("" ::: "memory");
    entry->high=executable?0:PAGE_NX;
    __asm__ volatile("" ::: "memory");
    entry->low=physical|flags;
}
#endif
