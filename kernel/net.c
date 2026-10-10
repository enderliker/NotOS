/* NotOS's own polled RTL8139 driver and bounded IPv4/ARP/UDP/DNS/TCP stack.
 * First target: QEMU user networking, guest 10.0.2.15/24, gateway 10.0.2.2.
 * One outstanding DNS/HTTP request, no heap, interrupts or host networking API.
 */
#include "base.h"
#include "net.h"
#define RX_RING 8192
#define FRAME_MAX 1514
static u16 local_tcp=49152;
#define LOCAL_DNS 49152
static u16 io;
static u32 pci_addr;
static u8 mac[6],gateway_mac[6];
static int gateway_known;
static volatile u8 rx[RX_RING+16+1536] __attribute__((aligned(16)));
static u8 tx[4][1536] __attribute__((aligned(16)));
static unsigned rx_pos,tx_slot;
static u8 frame[FRAME_MAX];
static const u8 local_ip[4]={10,0,2,15};
static const u8 gateway_ip[4]={10,0,2,2};
static const u8 dns_ip[4]={10,0,2,3};
static u8 remote_ip[4];
static char hostname[96];
static u16 remote_port=80;
static u16 dns_id;
static u32 seq,ack;
static unsigned now,deadline;
static unsigned tcp_bytes,header_used;
static char status_line[128];
static int status_printed,want_http;
enum {IDLE, WAIT_ARP, WAIT_DNS, WAIT_SYN, WAIT_HTTP};
static unsigned state;
static void copy(u8 *dst,const u8 *src,unsigned n) {while(n--) *dst++=*src++;}
static int same(const u8 *a,const u8 *b,unsigned n) {while(n--) if(*a++!=*b++) return 0;return 1;}
static u16 get16(const u8 *p) {return (u16)((p[0]<<8)|p[1]);}
static u32 get32(const u8 *p) {return ((u32)p[0]<<24)|((u32)p[1]<<16)|((u32)p[2]<<8)|p[3];}
static void set16(u8 *p,u16 v) {p[0]=(u8)(v>>8);p[1]=(u8)v;}
static void set32(u8 *p,u32 v) {p[0]=(u8)(v>>24);p[1]=(u8)(v>>16);p[2]=(u8)(v>>8);p[3]=(u8)v;}
static u32 sum(u32 total,const u8 *p,unsigned n) {
    while(n>1) {total+=get16(p);p+=2;n-=2;}
    if(n) total+=(u16)p[0]<<8;
    return total;
}
static u16 finish(u32 total) {
    while(total>>16) total=(total&65535)+(total>>16);
    return (u16)~total;
}
static u16 checksum(const u8 *p,unsigned n) {return finish(sum(0,p,n));}
static u16 transport_checksum(const u8 *src,const u8 *dst,u8 proto,const u8 *p,unsigned n) {
    u32 total=sum(0,src,4);total=sum(total,dst,4);total+=proto+n;
    return finish(sum(total,p,n));
}
static void ip_print(const u8 *ip) {
    for(unsigned i=0;i<4;i++) {if(i) console_putc('.');console_int(ip[i]);}
}
static void net_error(const char *message) {
    console_puts("NET ERROR: ");console_puts(message);console_putc('\n');state=IDLE;
}
static u32 pci_read(unsigned reg) {outl(0xcf8,pci_addr|(reg&0xfc));return inl(0xcfc);}
static int transmit(unsigned size) {
    if(size>FRAME_MAX) return 0;
    unsigned slot=tx_slot;
    if(!(inl(io+0x10+slot*4)&0x2000)) return 0;
    unsigned padded=size<60?60:size;
    copy(tx[slot],frame,size);
    for(unsigned i=size;i<padded;i++) tx[slot][i]=0;
    __asm__ volatile("" ::: "memory");
    outl(io+0x20+slot*4,(u32)(unsigned long)tx[slot]);
    outl(io+0x10+slot*4,(u32)padded);
    tx_slot=(slot+1)%4;
    return 1;
}
static void ethernet(u16 type,const u8 *dest) {
    copy(frame,dest,6);copy(frame+6,mac,6);set16(frame+12,type);
}
static void arp_request(void) {
    static const u8 broadcast[6]={255,255,255,255,255,255};
    ethernet(0x0806,broadcast);
    u8 *a=frame+14;
    set16(a,1);set16(a+2,0x0800);a[4]=6;a[5]=4;set16(a+6,1);
    copy(a+8,mac,6);copy(a+14,local_ip,4);
    for(unsigned i=0;i<6;i++) a[18+i]=0;
    copy(a+24,gateway_ip,4);
    if(!transmit(42)) net_error("transmit busy");
}
static void ip_header(u8 proto,const u8 *dest,unsigned payload_size) {
    ethernet(0x0800,gateway_mac);
    u8 *p=frame+14;
    for(unsigned i=0;i<20;i++) p[i]=0;
    p[0]=0x45;set16(p+2,(u16)(20+payload_size));set16(p+4,dns_id);
    set16(p+6,0x4000);p[8]=64;p[9]=proto;
    copy(p+12,local_ip,4);copy(p+16,dest,4);set16(p+10,checksum(p,20));
}
static void dns_send(void) {
    u8 query[128];
    for(unsigned i=0;i<12;i++) query[i]=0;
    set16(query,dns_id);set16(query+2,0x0100);set16(query+4,1);
    unsigned n=12;const char *p=hostname;
    while(*p) {
        unsigned begin=n++;unsigned len=0;
        while(*p && *p!='.') {query[n++]=(u8)*p++;len++;}
        query[begin]=(u8)len;
        if(*p=='.') p++;
    }
    query[n++]=0;set16(query+n,1);set16(query+n+2,1);n+=4;
    ip_header(17,dns_ip,n+8);
    u8 *udp=frame+34;
    set16(udp,LOCAL_DNS);set16(udp+2,53);set16(udp+4,(u16)(8+n));set16(udp+6,0);
    copy(udp+8,query,n);
    u16 c=transport_checksum(local_ip,dns_ip,17,udp,8+n);
    set16(udp+6,c?c:0xffff);
    state=WAIT_DNS;deadline=now+5000;
    if(!transmit(42+n)) net_error("transmit busy");
}
static void tcp_send(u8 flags,const char *payload,unsigned size) {
    ip_header(6,remote_ip,20+size);
    u8 *tcp=frame+34;
    for(unsigned i=0;i<20;i++) tcp[i]=0;
    set16(tcp,local_tcp);set16(tcp+2,remote_port);
    set32(tcp+4,seq);set32(tcp+8,ack);
    tcp[12]=0x50;tcp[13]=flags;set16(tcp+14,4096);
    if(size) copy(tcp+20,(const u8 *)payload,size);
    set16(tcp+16,transport_checksum(local_ip,remote_ip,6,tcp,20+size));
    if(!transmit(54+size)) net_error("transmit busy");
}
static void tcp_connect(void) {
    local_tcp++;
    if(local_tcp<49153) local_tcp=49153;
    seq=0x10203040+dns_id*1024;ack=0;
    tcp_bytes=header_used=0;status_printed=0;
    state=WAIT_SYN;deadline=now+8000;
    console_puts("NET: TCP connect ");ip_print(remote_ip);console_putc(':');console_int(remote_port);console_putc('\n');
    tcp_send(2,0,0);
}
static void begin_request(void) {
    if(remote_ip[0]) tcp_connect();else dns_send();
}
static int skip_name(const u8 *p,unsigned n,unsigned *offset) {
    unsigned i=*offset;
    while(i<n) {
        u8 len=p[i++];
        if(!len) {*offset=i;return 1;}
        if((len&0xc0)==0xc0) {
            if(i>=n) return 0;
            unsigned target=((len&0x3f)<<8)|p[i];
            if(target>=n) return 0;
            *offset=i+1;return 1;
        }
        if((len&0xc0)||len>63||i+len>n) return 0;
        i+=len;
    }
    return 0;
}
static void dns_receive(const u8 *p,unsigned n) {
    if(state!=WAIT_DNS || n<12 || get16(p)!=dns_id) return;
    u16 flags=get16(p+2);
    if(!(flags&0x8000)) return;
    if(flags&0x0200) {net_error("DNS truncated response unsupported");return;}
    if(flags&0x000f) {net_error("DNS server returned error");return;}
    unsigned offset=12,questions=get16(p+4),answers=get16(p+6);
    if(questions!=1 || answers>64) {net_error("invalid DNS counts");return;}
    if(!skip_name(p,n,&offset)||offset+4>n) {net_error("invalid DNS question");return;}
    offset+=4;
    for(unsigned i=0;i<answers;i++) {
        if(!skip_name(p,n,&offset)||offset+10>n) {net_error("invalid DNS answer");return;}
        u16 type=get16(p+offset),cls=get16(p+offset+2),size=get16(p+offset+8);
        offset+=10;
        if(offset+size>n) {net_error("invalid DNS length");return;}
        if(type==1 && cls==1 && size==4) {
            copy(remote_ip,p+offset,4);
            console_puts("DNS: ");console_puts(hostname);console_puts(" -> ");ip_print(remote_ip);console_putc('\n');
            if(want_http) tcp_connect();else state=IDLE;
            return;
        }
        offset+=size;
    }
    net_error("no IPv4 address in DNS answer");
}
static void http_request(void) {
    char request[256];unsigned n=0;
    const char *a="GET / HTTP/1.0\r\nHost: ";
    while(*a) request[n++]=*a++;
    for(unsigned i=0;hostname[i];i++) request[n++]=hostname[i];
    a="\r\nUser-Agent: NotOS/0.4\r\nConnection: close\r\n\r\n";
    while(*a) request[n++]=*a++;
    state=WAIT_HTTP;deadline=now+10000;
    tcp_send(0x18,request,n);seq+=n;
}
static void tcp_receive(const u8 *ip,const u8 *p,unsigned n) {
    if((state!=WAIT_SYN && state!=WAIT_HTTP)||n<20) return;
    if(!same(ip+12,remote_ip,4)||get16(p)!=remote_port||get16(p+2)!=local_tcp) return;
    unsigned h=(p[12]>>4)*4;
    if(h<20||h>n||transport_checksum(ip+12,ip+16,6,p,n)) return;
    u8 flags=p[13];u32 peer_seq=get32(p+4),peer_ack=get32(p+8);
    if(flags&4) {net_error("TCP connection reset");return;}
    if(state==WAIT_SYN) {
        if((flags&0x12)!=0x12 || peer_ack!=seq+1) return;
        seq++;ack=peer_seq+1;
        tcp_send(0x10,0,0);
        if(state!=IDLE) http_request();
        return;
    }
    if(!(flags&0x10)||peer_ack!=seq) return;
    unsigned size=n-h;
    if(peer_seq!=ack) {tcp_send(0x10,0,0);return;}
    if(size) {
        tcp_bytes+=size;
        for(unsigned i=0;i<size && !status_printed;i++) {
            char c=(char)p[h+i];
            if(c=='\n') {
                status_line[header_used]=0;
                console_puts("HTTP: ");console_puts(status_line);console_putc('\n');
                status_printed=1;
            } else if(c!='\r' && header_used<sizeof(status_line)-1) {
                status_line[header_used++]=(c>=32&&c<=126)?c:'?';
            }
        }
        ack+=size;deadline=now+10000;
    }
    if(flags&1) {
        ack++;
        tcp_send(0x10,0,0);
        tcp_send(0x11,0,0);seq++;
        console_puts("HTTP: received ");console_int((int)tcp_bytes);console_puts(" bytes\n");
        if(!status_printed) console_puts("NET ERROR: incomplete HTTP status line\n");
        state=IDLE;
    } else if(size) tcp_send(0x10,0,0);
}
static void receive(const u8 *p,unsigned n) {
    if(n<14) return;
    u16 type=get16(p+12);
    if(type==0x0806) {
        if(n<42) return;
        const u8 *a=p+14;
        if(get16(a)!=1||get16(a+2)!=0x0800||a[4]!=6||a[5]!=4) return;
        u16 op=get16(a+6);
        if(op==2 && same(a+14,gateway_ip,4) && same(a+24,local_ip,4) && same(a+18,mac,6)) {
            copy(gateway_mac,a+8,6);gateway_known=1;
            if(state==WAIT_ARP) begin_request();
        } else if(op==1 && same(a+24,local_ip,4)) {
            ethernet(0x0806,a+8);u8 *reply=frame+14;
            set16(reply,1);set16(reply+2,0x0800);reply[4]=6;reply[5]=4;set16(reply+6,2);
            copy(reply+8,mac,6);copy(reply+14,local_ip,4);copy(reply+18,a+8,6);copy(reply+24,a+14,4);
            transmit(42);
        }
        return;
    }
    if(type!=0x0800 || n<34) return;
    const u8 *ip=p+14;
    unsigned h=(ip[0]&15)*4,total=get16(ip+2);
    if((ip[0]>>4)!=4||h<20||h>60||n<14+h||total<h||total>n-14) return;
    if(checksum(ip,h)||!same(ip+16,local_ip,4)||(get16(ip+6)&0x3fff)) return;
    const u8 *data=ip+h;unsigned size=total-h;
    if(ip[9]==17 && size>=8 && same(ip+12,dns_ip,4)) {
        unsigned udp_size=get16(data+4);
        if(get16(data)!=53||get16(data+2)!=LOCAL_DNS||udp_size<8||udp_size>size) return;
        if(get16(data+6) && transport_checksum(ip+12,ip+16,17,data,udp_size)) return;
        dns_receive(data+8,udp_size-8);
    } else if(ip[9]==6) tcp_receive(ip,data,size);
}
void net_init(void) {
    io=0;
    for(unsigned device=0;device<32;device++) {
        pci_addr=0x80000000|(device<<11);
        if(pci_read(0)==0x813910ec) {
            u32 bar=pci_read(0x10);
            if(!(bar&1)) break;
            io=(u16)(bar&0xfffc);
            /* Only write command bits; preserve write-one-to-clear PCI status. */
            outl(0xcf8,pci_addr|4);outw(0xcfc,(u16)pci_read(4)|5);
            break;
        }
    }
    if(!io) {console_puts("NET: RTL8139 not present\n");return;}
    outb(io+0x52,0);outb(io+0x37,0x10);
    unsigned tries=100000;
    while((inb(io+0x37)&0x10) && --tries) {}
    if(!tries) {io=0;console_puts("NET ERROR: NIC reset timeout\n");return;}
    for(unsigned i=0;i<6;i++) mac[i]=inb(io+i);
    outw(io+0x3c,0);outw(io+0x3e,0xffff);
    outl(io+0x30,(u32)(unsigned long)rx);outw(io+0x38,0xfff0);
    outl(io+0x44,0x0000078f);outl(io+0x40,0x03000700);
    outb(io+0x37,0x0c);
    console_puts("NET: RTL8139 ready; IPv4 10.0.2.15\n");
}
void net_poll(unsigned now_ms) {
    now=now_ms;
    if(!io) return;
    unsigned budget=8;
    while(!(inb(io+0x37)&1) && budget--) {
        unsigned at=rx_pos;
        u16 status=(u16)(rx[at]|(rx[at+1]<<8));
        unsigned size=rx[at+2]|(rx[at+3]<<8);
        if(!(status&1)||size<4||size>1518) {
            /* Recover receiver without reading beyond the DMA allocation. */
            outb(io+0x37,4);rx_pos=0;outl(io+0x30,(u32)(unsigned long)rx);outw(io+0x38,0xfff0);outb(io+0x37,0x0c);
            net_error("invalid receive frame");break;
        }
        u8 packet[FRAME_MAX];
        for(unsigned i=0;i<size-4;i++) packet[i]=rx[at+4+i];
        rx_pos=(rx_pos+size+4+3)&~3u;rx_pos%=RX_RING;
        outw(io+0x38,(u16)(rx_pos-16));
        receive(packet,size-4);
    }
    outw(io+0x3e,0xffff);
    if(state!=IDLE && (int)(now-deadline)>=0) net_error("request timeout");
}
void net_status(void) {
    if(!io) {console_puts("NET: unavailable (use QEMU RTL8139)\n");return;}
    console_puts("NET: RTL8139 / IPv4 10.0.2.15\nGateway 10.0.2.2 / DNS 10.0.2.3\n");
    console_puts(state==IDLE ? "NET: idle\n" : "NET: request pending\n");
}
static int parse_ipv4(const char *s,u8 *dst) {
    for(unsigned i=0;i<4;i++) {
        if(*s<'0'||*s>'9') return 0;
        unsigned v=0,n=0;
        while(*s>='0'&&*s<='9') {if(++n>3) return 0;v=v*10+(unsigned)(*s++-'0');}
        if(v>255) return 0;
        dst[i]=(u8)v;
        if(i<3) {if(*s++!='.') return 0;}else if(*s) return 0;
    }
    return dst[0]!=0;
}
void net_command(const char *kind,const char *host) {
    if(!io) {net_error("RTL8139 not available");return;}
    if(state!=IDLE) {console_puts("NET ERROR: request already pending\n");return;}
    unsigned n=0,label=0;
    while(*host && *host!=':') {
        char c=*host++;
        if(n>=sizeof(hostname)-1) {net_error("hostname too long");return;}
        if(!((c>='a'&&c<='z')||(c>='A'&&c<='Z')||(c>='0'&&c<='9')||c=='-'||c=='.')) {
            net_error("invalid hostname");return;
        }
        if(c=='.') {if(!label) {net_error("empty DNS label");return;}label=0;}
        else if(++label>63) {net_error("DNS label too long");return;}
        hostname[n++]=c;
    }
    hostname[n]=0;
    if(!n||!label) {net_error("invalid hostname");return;}
    remote_port=80;
    if(*host==':') {
        host++;unsigned port=0,digits=0;
        while(*host>='0'&&*host<='9') {
            if(++digits>5) {net_error("invalid port");return;}
            port=port*10+(unsigned)(*host++-'0');
        }
        if(!digits||!port||port>65535||*host) {net_error("invalid port");return;}
        remote_port=(u16)port;
    }
    want_http=equal(kind,"http");
    for(unsigned i=0;i<4;i++) remote_ip[i]=0;
    int literal=parse_ipv4(hostname,remote_ip);
    if(!want_http && literal) {
        console_puts("DNS: literal ");ip_print(remote_ip);console_putc('\n');return;
    }
    if(!literal) for(unsigned i=0;i<4;i++) remote_ip[i]=0;
    dns_id++;deadline=now+5000;
    if(gateway_known) begin_request();else {state=WAIT_ARP;arp_request();}
}
