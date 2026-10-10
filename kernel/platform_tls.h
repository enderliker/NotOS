#ifndef NOTOS_PLATFORM_TLS_H
#define NOTOS_PLATFORM_TLS_H
#include "base.h"
struct utc_time {u32 days,seconds;};
int utc_convert(unsigned year,unsigned month,unsigned day,unsigned hour,unsigned minute,
    unsigned second,struct utc_time *time);
int rtc_utc(struct utc_time *time);
int entropy_init(void);
int entropy_get(void *buffer,u32 bytes);
int entropy_ready(void);
void secure_clear(void *buffer,u32 bytes);
#endif
