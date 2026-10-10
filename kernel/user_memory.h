#ifndef NOTOS_USER_MEMORY_H
#define NOTOS_USER_MEMORY_H
#include "base.h"
#define USER_BASE 0x08000000u
#define USER_SIZE 0x00400000u
int user_memory_check(const u32 *table,u32 pde,u32 address,u32 bytes,int writable);
#endif
