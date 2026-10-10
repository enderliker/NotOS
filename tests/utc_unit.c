#include <assert.h>
#include <stdio.h>
#include "../kernel/platform_tls.h"
int main(void) {
    struct utc_time time;
    assert(utc_convert(2000,1,1,0,0,0,&time) && time.days==730485 && time.seconds==0);
    assert(utc_convert(2000,2,29,23,59,59,&time) && time.days==730544 && time.seconds==86399);
    assert(!utc_convert(2001,2,29,0,0,0,&time));
    assert(!utc_convert(2026,4,31,0,0,0,&time));
    assert(!utc_convert(2026,13,1,0,0,0,&time));
    assert(!utc_convert(2026,1,1,24,0,0,&time));
    assert(!utc_convert(2026,1,1,0,0,60,&time));
    assert(!utc_convert(1999,1,1,0,0,0,&time));
    assert(!utc_convert(2100,1,1,0,0,0,&time));
    puts("PASS: Gregorian year-zero epoch, leap years and invalid UTC rejection");
}
