#include "platform_tls.h"
static u8 cmos(u8 index) {outb(0x70,index);return inb(0x71);}
static int snapshot(u8 values[8]) {
    for (unsigned n=0;n<100000;n++) {
        if (cmos(0x0a)&0x80) continue;
        u32 flags=irq_save();
        static const u8 indexes[]={0,2,4,7,8,9,0x32,0x0b};
        for (unsigned i=0;i<8;i++) values[i]=cmos(indexes[i]);
        int valid=!(cmos(0x0a)&0x80) && (cmos(0x0d)&0x80);
        irq_restore(flags);return valid;
    }
    return 0;
}
static int decimal(u8 byte) {
    if ((byte&15)>9 || (byte>>4)>9) return -1;
    return (byte>>4)*10+(byte&15);
}
int rtc_utc(struct utc_time *time) {
    for (unsigned attempt=0;attempt<8;attempt++) {
        u8 a[8],b[8];
        if (!snapshot(a) || !snapshot(b)) continue;
        unsigned i=0;while (i<8 && a[i]==b[i]) i++;
        if (i!=8) continue;
        int values[7];int pm=(a[2]&0x80)!=0;a[2]&=0x7f;
        for (i=0;i<7;i++) {
            values[i]=(a[7]&4)?a[i]:decimal(a[i]);
            if (values[i]<0) return 0;
        }
        if (!(a[7]&2)) {
            if (values[2]<1 || values[2]>12) return 0;
            values[2]=values[2]%12+(pm?12:0);
        }
        if (values[6]!=20) return 0;
        return utc_convert(2000u+(unsigned)values[5],(unsigned)values[4],(unsigned)values[3],
            (unsigned)values[2],(unsigned)values[1],(unsigned)values[0],time);
    }
    return 0;
}
