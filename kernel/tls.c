/* Nonblocking TLS 1.2/1.3 client; TCP only transports encrypted records. */
#include "tls.h"
#include "tls_profile.h"
#include "platform_tls.h"
#include "tls_platform_port.h"
#include "certificate_policy.h"
#include "heap.h"
#include <mbedtls/ssl.h>
#include <mbedtls/x509_crt.h>
#include <psa/crypto.h>
#include <string.h>
#ifndef NOTOS_TRUST_HEADER
#define NOTOS_TRUST_HEADER "../build/trust_roots.h"
#endif
#include NOTOS_TRUST_HEADER
#define QUEUE_SIZE 32768u
#define RESPONSE_LIMIT (16u*1024u*1024u)
struct record_queue {u8 bytes[QUEUE_SIZE];unsigned head,tail,used;};
struct tls_connection {
    mbedtls_ssl_context ssl;
    mbedtls_ssl_config config;
    mbedtls_x509_crt roots;
    struct record_queue input,output;
    char host[96],request[256],status[128];
    u8 plaintext[2048];
    unsigned request_bytes,request_offset,status_used,plaintext_bytes;
    int established,request_sent,status_printed,eof,result,closing,close_sent;
};
static struct tls_connection *connection;
extern int notos_tls_random(void *context,unsigned char *output,size_t bytes);
static int pending(int result) {return result==MBEDTLS_ERR_SSL_WANT_READ || result==MBEDTLS_ERR_SSL_WANT_WRITE;}
static void failed(const char *reason) {
    if (!connection || connection->result) return;
    console_puts("TLS ERROR: ");console_puts(reason);console_putc('\n');connection->result=-1;
}
static void protocol_error(int code) {
    console_puts("TLS ERROR: verification/protocol code=");console_int(code);console_putc('\n');connection->result=-1;
}
static void queue_write(struct record_queue *q,const u8 *data,unsigned bytes) {
    for (unsigned i=0;i<bytes;i++) {q->bytes[q->head++]=data[i];q->head%=QUEUE_SIZE;}q->used+=bytes;
}
static int receive_record(void *context,unsigned char *out,size_t capacity) {
    struct tls_connection *c=context;struct record_queue *q=&c->input;
    if (!q->used) return c->eof?0:MBEDTLS_ERR_SSL_WANT_READ;
    unsigned n=capacity>q->used?q->used:(unsigned)capacity;
    for (unsigned i=0;i<n;i++) {out[i]=q->bytes[q->tail++];q->tail%=QUEUE_SIZE;}q->used-=n;return (int)n;
}
static int send_record(void *context,const unsigned char *data,size_t bytes) {
    struct tls_connection *c=context;
    if (bytes>QUEUE_SIZE-c->output.used) return MBEDTLS_ERR_SSL_WANT_WRITE;
    queue_write(&c->output,data,(unsigned)bytes);return (int)bytes;
}
static int verify_certificate(void *context,mbedtls_x509_crt *certificate,int depth,uint32_t *flags) {
    (void)context;
    if (depth<0 || depth>7 || certificate->raw.len>16384 ||
        !certificate_policy_role(certificate->raw.p,(unsigned)certificate->raw.len,depth==0))
        *flags|=MBEDTLS_X509_BADCERT_OTHER;
    return 0; /* Never clear the backend's chain, name, time or signature errors. */
}
void tls_stop(void) {
    if (!connection) return;
    mbedtls_ssl_free(&connection->ssl);mbedtls_ssl_config_free(&connection->config);
    mbedtls_x509_crt_free(&connection->roots);
    secure_clear(connection,sizeof(*connection));kfree(connection);connection=0;
    mbedtls_psa_crypto_free(); /* Single TLS connection; release volatile PSA slots. */
}
int tls_start(const char *host) {
    struct utc_time utc;tls_stop();
    if (!host || !*host || length(host)>=96) {console_puts("TLS ERROR: invalid hostname\n");return 0;}
    if (!rtc_utc(&utc)) {console_puts("TLS ERROR: valid UTC RTC required\n");return 0;}
    if (!entropy_ready()) {console_puts("TLS ERROR: fresh firmware entropy required\n");return 0;}
    connection=kcalloc(1,sizeof(*connection));
    if (!connection) {console_puts("TLS ERROR: memory unavailable\n");return 0;}
    mbedtls_ssl_init(&connection->ssl);mbedtls_ssl_config_init(&connection->config);mbedtls_x509_crt_init(&connection->roots);
    memcpy(connection->host,host,length(host)+1);int error=0;unsigned offset=0;
    for (unsigned i=0;i<sizeof(trust_lengths)/sizeof(trust_lengths[0]);i++) {
        error=mbedtls_x509_crt_parse_der(&connection->roots,trust_der+offset,trust_lengths[i]);
        if (error) break;
        offset+=trust_lengths[i];
    }
    if (!error) error=psa_crypto_init();
    if (!error) error=tls_profile(&connection->config);
    if (!error) {
        mbedtls_ssl_conf_authmode(&connection->config,MBEDTLS_SSL_VERIFY_REQUIRED);
        mbedtls_ssl_conf_ca_chain(&connection->config,&connection->roots,0);
        mbedtls_ssl_conf_rng(&connection->config,notos_tls_random,0);
        mbedtls_ssl_conf_verify(&connection->config,verify_certificate,0);
        error=mbedtls_ssl_setup(&connection->ssl,&connection->config);
    }
    if (!error) error=mbedtls_ssl_set_hostname(&connection->ssl,host);
    if (error) {protocol_error(error);tls_stop();return 0;}
    mbedtls_ssl_set_bio(&connection->ssl,connection,send_record,receive_record,0);
    char *p=connection->request;
    const char *parts[]={"GET / HTTP/1.0\r\nHost: ",host,"\r\nUser-Agent: NotOS/0.14\r\nConnection: close\r\n\r\n"};
    for (unsigned i=0;i<3;i++) for (const char *s=parts[i];*s;s++) *p++=*s;
    connection->request_bytes=(unsigned)(p-connection->request);return 1;
}
int tls_feed(const u8 *data,unsigned bytes) {
    if (!connection || connection->result || bytes>QUEUE_SIZE-connection->input.used) return 0;
    queue_write(&connection->input,data,bytes);return 1;
}
const u8 *tls_output(unsigned *bytes) {
    *bytes=0;if (!connection || connection->result) return 0;
    struct record_queue *q=&connection->output;*bytes=q->used;
    if (*bytes>QUEUE_SIZE-q->tail) *bytes=QUEUE_SIZE-q->tail;
    return q->bytes+q->tail;
}
void tls_output_ack(unsigned bytes) {
    if (!connection || bytes>connection->output.used) return;
    connection->output.tail=(connection->output.tail+bytes)%QUEUE_SIZE;connection->output.used-=bytes;
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
    struct tls_connection *c=connection;
    if (!c->established) {
        int error=mbedtls_ssl_handshake(&c->ssl);
        if (pending(error)) return;
        if (error) {protocol_error(error);return;}
        if (mbedtls_ssl_get_verify_result(&c->ssl)) {failed("certificate verification rejected");return;}
        c->established=1;console_puts("TLS: verified ");console_puts(c->host);console_puts("; ");
        console_puts(mbedtls_ssl_get_version(&c->ssl));console_putc('\n');
    }
    if (!c->request_sent) {
        int n=mbedtls_ssl_write(&c->ssl,(u8 *)c->request+c->request_offset,c->request_bytes-c->request_offset);
        if (pending(n)) return;
        if (n<=0) {protocol_error(n);return;}
        c->request_offset+=(unsigned)n;c->request_sent=c->request_offset==c->request_bytes;
        if (!c->request_sent) return;
    }
    for (unsigned budget=0;budget<32 && !c->closing;budget++) {
        int n=mbedtls_ssl_read(&c->ssl,c->plaintext,sizeof(c->plaintext));
        if (n>0) {application(c->plaintext,(size_t)n);if (c->result) return;continue;}
        if (n==MBEDTLS_ERR_SSL_PEER_CLOSE_NOTIFY) {c->closing=1;break;}
        if (pending(n)) return;
        if (!n || n==MBEDTLS_ERR_SSL_CONN_EOF) failed("truncated TLS connection (no close_notify)");
        else protocol_error(n);
        return;
    }
    if (c->closing && !c->close_sent) {
        int error=mbedtls_ssl_close_notify(&c->ssl);
        if (pending(error)) return;
        if (error) {protocol_error(error);return;}c->close_sent=1;
    }
    if (c->close_sent && !c->output.used) {
        if (!c->status_printed) {failed("incomplete HTTPS response");return;}
        console_puts("HTTPS: received ");console_uint(c->plaintext_bytes);
        console_puts(" bytes; authenticated close_notify\n");c->result=1;
    }
}
int tls_result(void) {return connection?connection->result:-1;}
int tls_verified(void) {return connection && connection->request_sent;}
void tls_status(void) {
    struct utc_time utc;console_puts("TLS: version=1.2/1.3 entropy=");console_uint((u32)entropy_ready());
    console_puts(" rtc=");console_uint((u32)rtc_utc(&utc));console_puts(" roots=");
    console_uint(sizeof(trust_lengths)/sizeof(trust_lengths[0]));console_putc('\n');
}
