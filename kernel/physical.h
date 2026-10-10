#ifndef NOTOS_PHYSICAL_H
#define NOTOS_PHYSICAL_H
#include "physical_core.h"
int physical_init(void);
u32 physical_alloc(void);
int physical_free(u32 address);
int physical_usable(u32 address);
int physical_owned(u32 address);
unsigned physical_available(void);
void physical_status(void);
int physical_selftest(void);
#endif
