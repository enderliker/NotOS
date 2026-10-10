#ifndef NOTOS_DISK_H
#define NOTOS_DISK_H
#include "base.h"
int disk_init(void);
u32 disk_sectors(void);
int disk_read(u32 sector, void *buffer);
int disk_write(u32 sector, const void *buffer);
int disk_flush(void);
#endif
