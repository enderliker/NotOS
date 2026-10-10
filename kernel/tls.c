/* HTTPS state machine, isolated from Ethernet/TCP and from platform services. */
#include "tls.h"
#include "tls_profile.h"
#include "x509_policy.h"
#include "platform_tls.h"
#include "heap.h"
#include <string.h>
#ifndef NOTOS_TRUST_HEADER
#define NOTOS_TRUST_HEADER "../build/trust_roots.h"
#endif
#include NOTOS_TRUST_HEADER
#define INPUT_QUEUE 32768u
#define RESPONSE_LIMIT (16u*1024u*1024u)
struct tls_connection {
    br_ssl_client_context client;
    br_x509_minimal_context validator;
    struct x509_policy policy;
    u8 buffers[BR_SSL_BUFSIZE_BIDI];
    u8 input[INPUT_QUEUE];
    unsigned head,tail,used;
    char host[96],request[256],status[128];
    unsigned request_bytes,request_offset,status_used,plaintext_bytes;
    int request_sent,status_printed,eof,result;
};
static struct tls_connection *connection;
static void failed(const char *reason) {
    if (!connection || connection->result) return;
    console_puts("TLS ERROR: ");console_puts(reason);console_putc('\n');connection->result=-1;
}
void tls_stop(void) {
    if (connection) {secure_clear(connection,sizeof(*connection));kfree(connection);connection=0;}
}
int tls_start(const char *host) {
    struct utc_time utc;u8 seed[64];
    tls_stop();
    if (!host || !*host || length(host)>=96) {console_puts("TLS ERROR: invalid hostname\n");return 0;}
    if (!rtc_utc(&utc)) {console_puts("TLS ERROR: valid UTC RTC required\n");return 0;}
    if (!entropy_get(seed,sizeof(seed))) {console_puts("TLS ERROR: fresh firmware entropy required\n");return 0;}
    connection=kcalloc(1,sizeof(*connection));
    if (!connection) {secure_clear(seed,sizeof(seed));console_puts("TLS ERROR: memory unavailable\n");return 0;}
    memcpy(connection->host,host,length(host)+1);
    tls_profile(&connection->client,&connection->validator,trust_anchors,
        sizeof(trust_anchors)/sizeof(trust_anchors[0]));
    x509_policy_init(&connection->policy,&connection->validator);
    br_ssl_engine_set_x509(&connection->client.eng,&connection->policy.vtable);
    br_x509_minimal_set_time(&connection->validator,utc.days,utc.seconds);
    br_ssl_engine_set_buffer(&connection->client.eng,connection->buffers,sizeof(connection->buffers),1);
    br_ssl_engine_inject_entropy(&connection->client.eng,seed,sizeof(seed));
    secure_clear(seed,sizeof(seed));
    if (!br_ssl_client_reset(&connection->client,host,0)) {failed("initialization failed");tls_stop();return 0;}
    char *p=connection->request;
    const char *parts[]={"GET / HTTP/1.0\r\nHost: ",host,"\r\nUser-Agent: NotOS/0.10\r\nConnection: close\r\n\r\n"};
    for (unsigned i=0;i<3;i++) for (const char *s=parts[i];*s;s++) *p++=*s;
    connection->request_bytes=(unsigned)(p-connection->request);
    return 1;
}
int tls_feed(const u8 *bytes,unsigned size) {
    if (!connection || connection->result || size>INPUT_QUEUE-connection->used) return 0;
    for (unsigned i=0;i<size;i++) {connection->input[connection->head++]=bytes[i];connection->head%=INPUT_QUEUE;}
    connection->used+=size;return 1;
}
const u8 *tls_output(unsigned *size) {
    *size=0;
    if (!connection || connection->result) return 0;
    size_t available=0;const u8 *p=br_ssl_engine_sendrec_buf(&connection->client.eng,&available);
    *size=(unsigned)available;return p;
}
void tls_output_ack(unsigned size) {
    if (connection && !connection->result) br_ssl_engine_sendrec_ack(&connection->client.eng,size);
}
void tls_eof(void) {if (connection) connection->eof=1;}
static void application(const u8 *data,size_t bytes) {
    if (bytes>RESPONSE_LIMIT-connection->plaintext_bytes) {failed("response exceeds 16 MiB limit");return;}
    connection->plaintext_bytes+=(unsigned)bytes;
    for (size_t i=0;i<bytes && !connection->status_printed;i++) {
        char ch=(char)data[i];
        if (ch=='\n') {
            connection->status[connection->status_used]=0;
            if (!starts(connection->status,"HTTP/1.")) {failed("invalid HTTP status");return;}
            console_puts("HTTPS: ");console_puts(connection->status);console_putc('\n');connection->status_printed=1;
        } else if (ch!='\r') {
            if (connection->status_used==sizeof(connection->status)-1) {failed("HTTP status too long");return;}
            if (ch<32 || ch>126) {failed("invalid HTTP status");return;}
            connection->status[connection->status_used++]=ch;
        }
    }
}
void tls_poll(void) {
    if (!connection || connection->result) return;
    br_ssl_engine_context *engine=&connection->client.eng;
    for (unsigned budget=0;budget<128;budget++) {
        if (connection->policy.rejected) {failed("certificate DNS SAN/EKU/size policy rejected");return;}
        unsigned state=br_ssl_engine_current_state(engine);
        if (state&BR_SSL_CLOSED) {
            int error=br_ssl_engine_last_error(engine);
            if (error) {
                console_puts("TLS ERROR: verification/protocol code=");console_uint((u32)error);console_putc('\n');
                connection->result=-1;
            } else if (!connection->status_printed || !connection->request_sent) failed("incomplete HTTPS response");
            else {
                console_puts("HTTPS: received ");console_uint(connection->plaintext_bytes);
                console_puts(" bytes; authenticated close_notify\n");connection->result=1;
            }
            return;
        }
        size_t size=0;
        if (state&BR_SSL_RECVAPP) {
            const u8 *data=br_ssl_engine_recvapp_buf(engine,&size);application(data,size);
            if (connection->result) return;
            br_ssl_engine_recvapp_ack(engine,size);continue;
        }
        if ((state&BR_SSL_SENDAPP) && !connection->request_sent) {
            if (!connection->request_offset) {
                console_puts("TLS: verified ");console_puts(connection->host);console_puts("; TLS 1.2\n");
            }
            u8 *data=br_ssl_engine_sendapp_buf(engine,&size);
            unsigned remaining=connection->request_bytes-connection->request_offset;
            if (size>remaining) size=remaining;
            memcpy(data,connection->request+connection->request_offset,size);
            br_ssl_engine_sendapp_ack(engine,size);connection->request_offset+=(unsigned)size;
            if (connection->request_offset==connection->request_bytes) {
                connection->request_sent=1;br_ssl_engine_flush(engine,0);
            }
            continue;
        }
        if ((state&BR_SSL_RECVREC) && connection->used) {
            u8 *data=br_ssl_engine_recvrec_buf(engine,&size);
            if (size>connection->used) size=connection->used;
            if (size>INPUT_QUEUE-connection->tail) size=INPUT_QUEUE-connection->tail;
            memcpy(data,connection->input+connection->tail,size);
            connection->tail=(connection->tail+(unsigned)size)%INPUT_QUEUE;connection->used-=(unsigned)size;
            br_ssl_engine_recvrec_ack(engine,size);continue;
        }
        if (connection->eof && !connection->used && !(state&BR_SSL_SENDREC)) failed("truncated TLS connection (no close_notify)");
        return;
    }
}
int tls_result(void) {return connection?connection->result:-1;}
int tls_verified(void) {return connection && connection->request_sent;}
void tls_status(void) {
    struct utc_time utc;
    console_puts("TLS: version=1.2 entropy=");console_uint((u32)entropy_ready());
    console_puts(" rtc=");console_uint((u32)rtc_utc(&utc));
    console_puts(" roots=");console_uint(sizeof(trust_anchors)/sizeof(trust_anchors[0]));console_putc('\n');
}
