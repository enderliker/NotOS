/* Raw lwIP is entered only from the non-preemptive kernel, never an IRQ. */
#include "socket_stack.h"
#include "syscall_numbers.h"
#include "net.h"
#include "clock.h"
#include "platform_tls.h"
#include "lwip/init.h"
#include "lwip/netif.h"
#include "lwip/etharp.h"
#include "lwip/tcp.h"
#include "lwip/udp.h"
#include "lwip/dns.h"
#include "lwip/timeouts.h"
#include "lwip/stats.h"
#include "lwip/memp.h"
#include "lwip/priv/tcp_priv.h"
#include "netif/ethernet.h"
#include <string.h>
enum connection_state {CONNECTION_NEW,CONNECTION_PENDING,CONNECTION_READY,CONNECTION_FAILED};
struct endpoint {
    u32 used,state,head,count,eof,read_closed,write_closed,error,last_error;
    u32 receive_timeout,send_timeout,nodelay;
    struct notos_socket_address local,remote;
    struct tcp_pcb *pcb;
    u8 receive[NOTOS_SOCKET_BUFFER];
};
struct query {u32 token,started,done,error;struct fd_table *owner;u8 address[4];};
static struct endpoint endpoints[NOTOS_SOCKET_COUNT];
static struct query queries[4];
static struct netif interface;
static int initialized;
static u32 next_query=1,drop_mask;
static u16 next_port=52000;
u32_t sys_now(void) {return clock_ms();}
void socket_stack_assert(const char *message) {
    console_puts("SOCKET ASSERT: ");console_puts(message);console_putc('\n');
    for (;;) __asm__ volatile("cli; hlt");
}
u32 socket_stack_random(void) {
    u32 result;
    if (!entropy_get(&result,sizeof(result))) socket_stack_assert("entropy unavailable");
    return result;
}
static int translate(err_t error) {
    switch (error) {
    case ERR_OK:return 0;
    case ERR_MEM:case ERR_BUF:return ERROR_AGAIN;
    case ERR_TIMEOUT:return ERROR_TIMED_OUT;
    case ERR_RTE:return ERROR_NETWORK_UNREACHABLE;
    case ERR_RST:case ERR_ABRT:return ERROR_CONNECTION_RESET;
    case ERR_CLSD:case ERR_CONN:return ERROR_NOT_CONNECTED;
    case ERR_USE:return ERROR_ADDRESS_IN_USE;
    default:return ERROR_IO;
    }
}
static struct endpoint *get(int handle) {
    return handle>=0 && (u32)handle<NOTOS_SOCKET_COUNT && endpoints[handle].used?&endpoints[handle]:0;
}
static void failed(void *argument,err_t error) {
    struct endpoint *e=argument;int opening=e->state==CONNECTION_PENDING;
    e->pcb=0;
    if (error==ERR_CLSD && e->eof) return;
    e->state=CONNECTION_FAILED;
    e->error=e->last_error=(u32)(error==ERR_RST && opening?ERROR_CONNECTION_REFUSED:translate(error));
}
static void detach(struct tcp_pcb *pcb) {
    tcp_arg(pcb,0);tcp_recv(pcb,0);tcp_err(pcb,0);tcp_sent(pcb,0);tcp_poll(pcb,0,0);
}
static err_t received(void *argument,struct tcp_pcb *pcb,struct pbuf *p,err_t error) {
    struct endpoint *e=argument;(void)error;
    if (!p) {
        e->eof=1;
        /* TIME_WAIT reclamation has no raw error callback: retain only cached identity. */
        if (e->write_closed) {detach(pcb);e->pcb=0;}
        return ERR_OK;
    }
    if (e->read_closed) {tcp_recved(pcb,p->tot_len);pbuf_free(p);return ERR_OK;}
    if (p->tot_len>NOTOS_SOCKET_BUFFER-e->count) return ERR_MEM;
    u32 offset=(e->head+e->count)%NOTOS_SOCKET_BUFFER,first=NOTOS_SOCKET_BUFFER-offset;
    if (first>p->tot_len) first=p->tot_len;
    pbuf_copy_partial(p,e->receive+offset,(u16_t)first,0);
    if (first<p->tot_len) pbuf_copy_partial(p,e->receive,(u16_t)(p->tot_len-first),(u16_t)first);
    e->count+=p->tot_len;pbuf_free(p);return ERR_OK;
}
static err_t connected(void *argument,struct tcp_pcb *pcb,err_t error) {
    struct endpoint *e=argument;
    if (error) {e->state=CONNECTION_FAILED;e->error=e->last_error=(u32)translate(error);}
    else {
        e->state=CONNECTION_READY;
        memcpy(e->local.ip,ip_2_ip4(&pcb->local_ip),4);e->local.port=pcb->local_port;
        memcpy(e->remote.ip,ip_2_ip4(&pcb->remote_ip),4);e->remote.port=pcb->remote_port;
    }
    return ERR_OK;
}
static err_t output_poll(void *argument,struct tcp_pcb *pcb) {(void)argument;tcp_output(pcb);return ERR_OK;}
static err_t link_output(struct netif *netif,struct pbuf *p) {
    (void)netif;u8 frame[1514];
    if (p->tot_len>sizeof(frame)) return ERR_BUF;
    pbuf_copy_partial(p,frame,p->tot_len,0);
    if (p->tot_len>=54 && frame[12]==8 && !frame[13] && frame[23]==6) {
        u32 h=(frame[14]&15u)*4u,t=14+h;
        if (t+20<=p->tot_len) {
            u32 mask=(frame[t+13]&2)?1u:p->tot_len>t+(frame[t+12]>>4)*4u?2u:0;
            if (mask && (drop_mask&mask)) {drop_mask&=~mask;console_puts("SOCKET TEST: dropped transmit\n");return ERR_OK;}
        }
    }
    return net_frame_send(frame,p->tot_len)?ERR_OK:ERR_IF;
}
static err_t interface_init(struct netif *netif) {
    netif->name[0]='n';netif->name[1]='o';netif->mtu=1500;
    netif->flags=NETIF_FLAG_BROADCAST|NETIF_FLAG_ETHARP;
    netif->output=etharp_output;netif->linkoutput=link_output;return ERR_OK;
}
static void address4(ip4_addr_t *out,const u8 bytes[4]) {IP4_ADDR(out,bytes[0],bytes[1],bytes[2],bytes[3]);}
void socket_stack_init(const u8 mac[6],const u8 ip[4],const u8 gateway[4],const u8 dns[4]) {
    if (initialized || !entropy_ready()) return;
    lwip_init();ip4_addr_t address,mask,router;
    address4(&address,ip);IP4_ADDR(&mask,255,255,255,0);address4(&router,gateway);
    if (!netif_add(&interface,&address,&mask,&router,0,interface_init,ethernet_input)) return;
    interface.hwaddr_len=6;memcpy(interface.hwaddr,mac,6);
    netif_set_default(&interface);netif_set_up(&interface);netif_set_link_up(&interface);
    initialized=1;next_port=(u16_t)(52000+socket_stack_random()%9000);
    socket_stack_configure(ip,gateway,dns);
}
void socket_stack_configure(const u8 ip[4],const u8 gateway[4],const u8 dns[4]) {
    if (!initialized) return;
    ip4_addr_t address,mask,router;ip_addr_t server;
    address4(&address,ip);IP4_ADDR(&mask,255,255,255,0);address4(&router,gateway);
    netif_set_addr(&interface,&address,&mask,&router);
    IP_ADDR4(&server,dns[0],dns[1],dns[2],dns[3]);dns_setserver(0,&server);
}
int socket_stack_input(const void *frame,u32 bytes) {
    const u8 *p=frame;int consumed=0;
    if (!initialized || bytes<14) return 0;
    u16 type=(u16)((p[12]<<8)|p[13]);
    if (type==0x0806) consumed=0;
    else if (type==0x0800 && bytes>=34) {
        u32 h=(p[14]&15u)*4u;
        if (h<20 || h>60 || bytes<14+h+4) return 0;
        u16 port=(u16)((p[14+h+2]<<8)|p[14+h+3]);
        if (p[23]==6) {if (port<52000 || port>=61000) return 0;consumed=1;}
        else if (p[23]==17) {if (port<61000 || port>=62000) return 0;consumed=1;}
        else if (p[23]==1) consumed=1;
        else return 0;
    } else return 0;
    if (consumed && type==0x0800 && p[23]==6 && (drop_mask&4u)) {
        drop_mask&=~4u;console_puts("SOCKET TEST: dropped receive\n");return 1;
    }
    struct pbuf *buffer=pbuf_alloc(PBUF_RAW,(u16_t)bytes,PBUF_POOL);
    if (!buffer) return consumed;
    if (pbuf_take(buffer,frame,(u16_t)bytes)!=ERR_OK || interface.input(buffer,&interface)!=ERR_OK) pbuf_free(buffer);
    return consumed;
}
void socket_stack_poll(void) {if (initialized) sys_check_timeouts();}
int socket_stack_busy(void) {
    if (!initialized) return 0;
    for (u32 i=0;i<NOTOS_SOCKET_COUNT;i++) if (endpoints[i].used) return 1;
    for (u32 i=0;i<4;i++) if (queries[i].token) return 1;
    return tcp_active_pcbs || tcp_tw_pcbs;
}
int socket_create(void) {
    if (!initialized) return entropy_ready()?-ERROR_NETWORK_UNREACHABLE:-ERROR_IO;
    for (u32 i=0;i<NOTOS_SOCKET_COUNT;i++) if (!endpoints[i].used) {
        /* Never let tcp_new evict an owned active connection under pool pressure. */
        if (lwip_stats.memp[MEMP_TCP_PCB]->used>=lwip_stats.memp[MEMP_TCP_PCB]->avail) {
            struct tcp_pcb *candidate=tcp_tw_pcbs;
            while (candidate) {
                int pinned=0;
                for (u32 j=0;j<NOTOS_SOCKET_COUNT;j++) if (endpoints[j].used && endpoints[j].pcb==candidate) pinned=1;
                if (!pinned) break;
                candidate=candidate->next;
            }
            if (candidate) tcp_abort(candidate);else return -ERROR_MEMORY;
        }
        struct tcp_pcb *pcb=tcp_new_ip_type(IPADDR_TYPE_V4);if (!pcb) return -ERROR_MEMORY;
        endpoints[i]=(struct endpoint){.used=1,.pcb=pcb};
        tcp_arg(pcb,&endpoints[i]);tcp_recv(pcb,received);tcp_err(pcb,failed);tcp_poll(pcb,output_poll,2);
        return (int)i;
    }
    return -ERROR_TOO_MANY_FILES;
}
void socket_release(int handle) {
    struct endpoint *e=get(handle);if (!e) return;
    if (e->pcb) {struct tcp_pcb *pcb=e->pcb;detach(pcb);if (tcp_close(pcb)!=ERR_OK) tcp_abort(pcb);}
    *e=(struct endpoint){0};
}
int socket_connect(int handle,const struct notos_socket_address *address) {
    struct endpoint *e=get(handle);if (!e) return -ERROR_NOT_SOCKET;
    if (e->state==CONNECTION_PENDING) return -ERROR_ALREADY;
    if (e->state==CONNECTION_READY) return -ERROR_IS_CONNECTED;
    if (e->state==CONNECTION_FAILED) return -(int)e->error;
    if (!address->port || address->port>65535 || !address->ip[0] || address->ip[0]>=224) return -ERROR_INVALID_ARGUMENT;
    err_t error=ERR_USE;
    for (u32 tries=0;tries<9000 && error==ERR_USE;tries++) {
        if (next_port>=61000) next_port=52000;
        error=tcp_bind(e->pcb,IP_ANY_TYPE,next_port++);
    }
    if (error) return -translate(error);
    ip_addr_t ip;IP_ADDR4(&ip,address->ip[0],address->ip[1],address->ip[2],address->ip[3]);
    error=tcp_connect(e->pcb,&ip,(u16_t)address->port,connected);
    if (error) return -translate(error);
    e->state=CONNECTION_PENDING;return -ERROR_IN_PROGRESS;
}
int socket_connected(int handle) {
    struct endpoint *e=get(handle);if (!e) return -ERROR_NOT_SOCKET;
    if (e->state==CONNECTION_READY) return 0;
    return e->state==CONNECTION_FAILED?-(int)e->error:-ERROR_IN_PROGRESS;
}
int socket_read(int handle,void *buffer,u32 bytes,u32 flags) {
    struct endpoint *e=get(handle);if (!e) return -ERROR_NOT_SOCKET;
    if (!bytes || e->read_closed) return 0;
    if (e->count) {
        if (bytes>e->count) bytes=e->count;
        for (u32 i=0;i<bytes;i++) ((u8 *)buffer)[i]=e->receive[(e->head+i)%NOTOS_SOCKET_BUFFER];
        if (!(flags&NOTOS_MSG_PEEK)) {
            e->head=(e->head+bytes)%NOTOS_SOCKET_BUFFER;e->count-=bytes;
            if (e->pcb) tcp_recved(e->pcb,(u16_t)bytes);
        }
        return (int)bytes;
    }
    if (e->eof) return 0;
    if (e->state==CONNECTION_FAILED) return -(int)e->error;
    if (e->state!=CONNECTION_READY) return -ERROR_NOT_CONNECTED;
    return -ERROR_AGAIN;
}
int socket_write(int handle,const void *buffer,u32 bytes) {
    struct endpoint *e=get(handle);if (!e) return -ERROR_NOT_SOCKET;
    if (e->write_closed || e->state==CONNECTION_FAILED) return -ERROR_BROKEN_PIPE;
    if (e->state!=CONNECTION_READY) return -ERROR_NOT_CONNECTED;
    if (!bytes) return 0;
    u32 space=tcp_sndbuf(e->pcb);if (!space) return -ERROR_AGAIN;
    if (bytes>space) bytes=space;
    err_t error=tcp_write(e->pcb,buffer,(u16_t)bytes,TCP_WRITE_FLAG_COPY);
    if (error) return -translate(error);
    tcp_output(e->pcb);return (int)bytes;
}
int socket_shutdown(int handle,u32 how) {
    struct endpoint *e=get(handle);if (!e) return -ERROR_NOT_SOCKET;
    if (how>2) return -ERROR_INVALID_ARGUMENT;
    if (e->state!=CONNECTION_READY) return -ERROR_NOT_CONNECTED;
    /* Avoid TF_RXCLOSED: it suppresses the raw PCB-free notification. */
    if (how!=0 && !e->write_closed) {
        err_t error=tcp_shutdown(e->pcb,0,1);if (error) return -translate(error);
        e->write_closed=1;
    }
    if (how!=1) {
        if (e->count && e->pcb) tcp_recved(e->pcb,(u16_t)e->count);
        e->read_closed=1;e->count=0;
    }
    return 0;
}
int socket_get_option(int handle,u32 option,u32 *value) {
    struct endpoint *e=get(handle);if (!e) return -ERROR_NOT_SOCKET;
    switch (option) {
    case NOTOS_OPT_ERROR:*value=e->last_error;e->last_error=0;return 0;
    case NOTOS_OPT_TYPE:*value=NOTOS_SOCK_STREAM;return 0;
    case NOTOS_OPT_RCVTIMEO:*value=e->receive_timeout;return 0;
    case NOTOS_OPT_SNDTIMEO:*value=e->send_timeout;return 0;
    case NOTOS_OPT_RCVBUF:case NOTOS_OPT_SNDBUF:*value=NOTOS_SOCKET_BUFFER;return 0;
    case NOTOS_OPT_NODELAY:*value=e->nodelay;return 0;
    default:return -ERROR_PROTOCOL_OPTION;
    }
}
int socket_set_option(int handle,u32 option,u32 value) {
    struct endpoint *e=get(handle);if (!e) return -ERROR_NOT_SOCKET;
    if (option==NOTOS_OPT_RCVTIMEO || option==NOTOS_OPT_SNDTIMEO) {
        if (value>60000) return -ERROR_INVALID_ARGUMENT;
        if (option==NOTOS_OPT_RCVTIMEO) e->receive_timeout=value;else e->send_timeout=value;return 0;
    }
    if (option==NOTOS_OPT_NODELAY) {
        if (!e->pcb || value>1) return -ERROR_INVALID_ARGUMENT;
        if (value) tcp_nagle_disable(e->pcb);else tcp_nagle_enable(e->pcb);e->nodelay=value;return 0;
    }
    return -ERROR_PROTOCOL_OPTION;
}
u32 socket_timeout(int handle,int writing) {struct endpoint *e=get(handle);return e?(writing?e->send_timeout:e->receive_timeout):0;}
void socket_connect_timeout(int handle) {
    struct endpoint *e=get(handle);if (!e) return;
    if (e->pcb) {struct tcp_pcb *pcb=e->pcb;e->pcb=0;tcp_err(pcb,0);tcp_abort(pcb);}
    e->state=CONNECTION_FAILED;e->error=e->last_error=ERROR_TIMED_OUT;
}
int socket_get_address(int handle,int peer,struct notos_socket_address *address) {
    struct endpoint *e=get(handle);if (!e) return -ERROR_NOT_SOCKET;
    if (peer && e->state!=CONNECTION_READY) return -ERROR_NOT_CONNECTED;
    if (e->pcb) {
        memcpy(address->ip,ip_2_ip4(peer?&e->pcb->remote_ip:&e->pcb->local_ip),4);
        address->port=peer?e->pcb->remote_port:e->pcb->local_port;
    } else *address=peer?e->remote:e->local;
    return 0;
}
static void resolved(const char *name,const ip_addr_t *address,void *argument) {
    (void)name;u32 token=(u32)(unsigned long)argument;
    for (u32 i=0;i<4;i++) if (queries[i].token==token) {
        queries[i].done=1;queries[i].error=address?0:ERROR_NOT_FOUND;
        if (address) memcpy(queries[i].address,ip_2_ip4(address),4);
        return;
    }
}
int socket_resolve_start(struct fd_table *owner,const char *name) {
    if (!initialized) return -ERROR_NETWORK_UNREACHABLE;
    for (u32 j=0;j<4;j++) if (queries[j].token && queries[j].owner==owner) return -ERROR_AGAIN;
    u32 i=0;while (i<4 && queries[i].token) i++;
    if (i==4 || !next_query || next_query>0x7fffffffu) return -ERROR_AGAIN;
    u32 token=next_query++;
    queries[i]=(struct query){.token=token,.started=clock_ms(),.owner=owner};
    ip_addr_t result;
    err_t error=dns_gethostbyname_addrtype(name,&result,resolved,(void *)(unsigned long)token,LWIP_DNS_ADDRTYPE_IPV4);
    if (!error) resolved(name,&result,(void *)(unsigned long)token);
    else if (error!=ERR_INPROGRESS) {queries[i]=(struct query){0};return -translate(error);}
    return (int)token;
}
int socket_resolve_result(struct fd_table *owner,u32 token,u8 result[4]) {
    for (u32 i=0;i<4;i++) if (queries[i].token==token && queries[i].owner==owner) {
        struct query *query=&queries[i];
        if (!query->done && (u32)(clock_ms()-query->started)<8000) return -ERROR_AGAIN;
        int error=query->done?(int)query->error:ERROR_TIMED_OUT;
        if (!error) memcpy(result,query->address,4);
        *query=(struct query){0};return -error;
    }
    return -ERROR_INVALID_ARGUMENT;
}
void socket_resolve_cancel(struct fd_table *owner) {for (u32 i=0;i<4;i++) if (queries[i].owner==owner) queries[i]=(struct query){0};}
void socket_stack_status(void) {
    u32 opened=0,pending=0;
    for (u32 i=0;i<NOTOS_SOCKET_COUNT;i++) opened+=endpoints[i].used;
    for (u32 i=0;i<4;i++) pending+=queries[i].token!=0;
    console_puts("SOCKETS: ready=");console_uint((u32)initialized);console_puts(" open=");console_uint(opened);
    console_puts(" queries=");console_uint(pending);console_putc('\n');
}
void socket_stack_drop(const char *kind) {
    u32 mask=equal(kind,"syn")?1u:equal(kind,"tx")?2u:equal(kind,"rx")?4u:0;
    if (!mask) {console_puts("SOCKET ERROR: choose syn/tx/rx\n");return;}
    drop_mask=mask;console_puts("SOCKET TEST: one-packet loss armed\n");
}
