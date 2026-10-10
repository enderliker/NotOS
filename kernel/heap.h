#ifndef NOTOS_HEAP_H
#define NOTOS_HEAP_H
#include "heap_core.h"
#define HEAP_BYTES (1024u*1024u)
#define HEAP_BASE 0xd0001000u
int heap_init(void);
void *kmalloc(u32 bytes);
void *kcalloc(u32 count,u32 bytes);
int kfree(void *pointer);
void heap_status(void);
int heap_selftest(void);
void heap_command(const char *kind,const char *argument);
void heap_fault_guard_end(void);
#endif
