/* ATA PIO, primary slave only: the boot disk is never a write target. */
#include "disk.h"
#define ATA_DATA 0x1f0
#define ATA_STATUS 0x1f7
#define POLL_LIMIT 1000000u
static u32 capacity;

static int wait_ready(int data) {
    for (unsigned i = 0; i < POLL_LIMIT; ++i) {
        u8 status = inb(ATA_STATUS);
        if (!status || status == 0xff) return 0;
        if (status & 0x80) continue;
        if (status & 0x21) return 0;
        if (!data || (status & 8)) return 1;
    }
    return 0;
}
static void select_slave(u32 sector) {
    outb(0x1f6, (u8)(0xf0 | ((sector >> 24) & 15)));
    for (unsigned i = 0; i < 4; ++i) (void)inb(0x3f6);
}
int disk_init(void) {
    capacity = 0;
    select_slave(0);
    outb(0x1f2, 0); outb(0x1f3, 0); outb(0x1f4, 0); outb(0x1f5, 0);
    outb(ATA_STATUS, 0xec);
    if (!wait_ready(1) || inb(0x1f4) || inb(0x1f5)) return 0;
    u16 words[256];
    for (unsigned i = 0; i < 256; ++i) words[i] = inw(ATA_DATA);
    if ((words[0] & 0x8000) || !(words[49] & 0x200)) return 0;
    capacity = (u32)words[60] | ((u32)words[61] << 16);
    if (capacity > 0x10000000u) capacity = 0x10000000u;
    return capacity != 0;
}
u32 disk_sectors(void) { return capacity; }
static int transfer(u32 sector, void *buffer, int writing) {
    if (!buffer || sector >= capacity) return 0;
    select_slave(sector);
    if (!wait_ready(0)) return 0;
    outb(0x1f2, 1); outb(0x1f3, (u8)sector);
    outb(0x1f4, (u8)(sector >> 8)); outb(0x1f5, (u8)(sector >> 16));
    outb(ATA_STATUS, writing ? 0x30 : 0x20);
    if (!wait_ready(1)) return 0;
    u16 *words = buffer;
    for (unsigned i = 0; i < 256; ++i) {
        if (writing) outw(ATA_DATA, words[i]); else words[i] = inw(ATA_DATA);
    }
    return wait_ready(0);
}
int disk_read(u32 sector, void *buffer) { return transfer(sector, buffer, 0); }
int disk_write(u32 sector, const void *buffer) { return transfer(sector, (void *)buffer, 1); }
int disk_flush(void) {
    if (!capacity) return 0;
    select_slave(0); outb(ATA_STATUS, 0xe7);
    return wait_ready(0);
}
