/* Pure protocol helpers tested natively; driver and traffic need QEMU. */
#include <assert.h>
#include <stdio.h>
#include "../kernel/net.c"
void console_putc(char c) {(void)c;}
void console_puts(const char *s) {(void)s;}
void console_int(int v) {(void)v;}
int main(void) {
    u8 bytes[]={0x00,0x01,0xf2,0x03,0xf4,0xf5,0xf6,0xf7};
    assert(checksum(bytes,sizeof(bytes))==0x220d);
    u8 ip[4];
    assert(parse_ipv4("10.0.2.2",ip) && ip[0]==10 && ip[3]==2);
    assert(!parse_ipv4("256.0.0.1",ip));
    assert(!parse_ipv4("10.0.2",ip));
    assert(!parse_ipv4("10.0.2.2x",ip));
    assert(!parse_ipv4("0000.0.0.1",ip));
    u8 name[]={3,'w','w','w',0};unsigned offset=0;
    assert(skip_name(name,sizeof(name),&offset) && offset==5);
    offset=0;assert(!skip_name(name,3,&offset));
    u8 compressed[]={0xc0,0};offset=0;
    assert(skip_name(compressed,2,&offset) && offset==2);
    offset=0;assert(!skip_name(compressed,1,&offset));
    u8 bad[]={0xc0,50};offset=0;assert(!skip_name(bad,2,&offset));
    /* Every short DNS message must be safely ignored. */
    for(unsigned n=0;n<12;n++) {state=WAIT_DNS;dns_receive(bytes,n<sizeof(bytes)?n:sizeof(bytes));}
    puts("PASS: checksums, IPv4 parsing and bounded DNS decoding");
}
