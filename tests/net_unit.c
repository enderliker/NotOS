/* Pure protocol helpers tested natively; driver and traffic need QEMU. */
#include <assert.h>
#include <stdio.h>
#include "../kernel/net.c"
void console_putc(char c) {(void)c;}
void console_puts(const char *s) {(void)s;}
void console_int(int v) {(void)v;}
int tls_start(const char *host) {(void)host;return 0;}
void tls_stop(void) {}
void tls_poll(void) {}
int tls_result(void) {return -1;}
int tls_verified(void) {return 0;}
int tls_feed(const u8 *data,unsigned bytes) {(void)data;(void)bytes;return 0;}
const u8 *tls_output(unsigned *bytes) {*bytes=0;return 0;}
void tls_output_ack(unsigned bytes) {(void)bytes;}
void tls_eof(void) {}
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
    struct tcp_retry r;
    tcp_retry_begin(&r,10,0xfffffff0u);
    assert(!tcp_retry_due(&r,0xfffffff1u));
    assert(tcp_retry_due(&r,484));
    tcp_retry_ack(&r,9,10);assert(r.active);
    tcp_retry_ack(&r,11,10);assert(r.active);
    tcp_retry_ack(&r,10,10);assert(!r.active);
    tcp_retry_begin(&r,2,0);
    tcp_retry_ack(&r,0xfffffffeu,2);assert(r.active);
    for(unsigned i=0;i<TCP_RETRY_LIMIT;i++) assert(tcp_retry_advance(&r,r.deadline));
    assert(!tcp_retry_advance(&r,r.deadline) && !r.active);
    io=1;state=IDLE;net_configure("10.0.2.16 10.0.2.2 10.0.2.3");
    assert(local_ip[3]==16 && !gateway_known);
    net_configure("224.1.2.3 224.1.2.2 10.0.2.3");assert(local_ip[3]==16);
    net_configure("10.0.2.17 10.0.3.2 10.0.2.3");assert(local_ip[3]==16);
    puts("PASS: bounded retry backoff, ACK validation/wrap and static config validation");
    puts("PASS: checksums, IPv4 parsing and bounded DNS decoding");
}
