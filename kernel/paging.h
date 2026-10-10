#ifndef NOTOS_PAGING_H
#define NOTOS_PAGING_H
#include "base.h"
#define VM_BASE 0xd0000000u
#define VM_SIZE 0x00400000u
int paging_init(void);
int paging_user_map(u32 virtual_address,u32 physical_address,int writable);
u32 paging_user_unmap(u32 virtual_address);
int paging_map(u32 virtual_address,u32 physical_address,int writable);
u32 paging_unmap(u32 virtual_address);
u32 paging_translate(u32 virtual_address);
void paging_status(void);
int paging_selftest(void);
void paging_fault_null(void);
void paging_fault_readonly(void);
void paging_fault_guard(void);
void paging_fault_unmapped(void);
#endif
