#include "platform_tls.h"
static int leap(unsigned year) {return year%4==0 && (year%100!=0 || year%400==0);}
int utc_convert(unsigned year,unsigned month,unsigned day,unsigned hour,unsigned minute,
    unsigned second,struct utc_time *time) {
    static const unsigned lengths[]={31,28,31,30,31,30,31,31,30,31,30,31};
    if (!time || year<2000 || year>2099 || month<1 || month>12 || day<1
        || hour>23 || minute>59 || second>59) return 0;
    unsigned maximum=lengths[month-1]+(month==2 && leap(year));
    if (day>maximum) return 0;
    u32 days=365u*year+(year+3)/4-(year+99)/100+(year+399)/400;
    for (unsigned i=1;i<month;i++) days+=lengths[i-1]+(i==2 && leap(year));
    time->days=days+day-1;time->seconds=hour*3600+minute*60+second;return 1;
}
