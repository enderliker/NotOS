/* BSD-shaped IPv4 client API over NotOS' own wire ABI, never a Linux ABI. */
#include "os.h"
#include "../../kernel/socket_abi.h"
#include <arpa/inet.h>
#include <netinet/tcp.h>
#include <netdb.h>
#include <sys/time.h>
#include <errno.h>
#include <limits.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
static int range(const void *p,size_t n,int write) {
    return notos_checked(notos_raw_call(SYS_USER_CHECK,(uint32_t)p,n,(uint32_t)write));
}
static int string_range(const char *s,size_t limit) {
    for (size_t i=0;i<limit;i++) {
        if ((uintptr_t)s>UINTPTR_MAX-i || range((const void *)((uintptr_t)s+i),1,0)) return -1;
        if (!s[i]) return 0;
    }
    errno=ENAMETOOLONG;return -1;
}
uint16_t htons(uint16_t n) {return (uint16_t)((n<<8)|(n>>8));}
uint16_t ntohs(uint16_t n) {return htons(n);}
uint32_t htonl(uint32_t n) {return __builtin_bswap32(n);}
uint32_t ntohl(uint32_t n) {return htonl(n);}
int inet_pton(int family,const char *text,void *output) {
    if (family!=AF_INET) {errno=EAFNOSUPPORT;return -1;}
    if (string_range(text,16) || range(output,4,1)) return -1;
    uint8_t address[4];
    for (unsigned i=0;i<4;i++) {
        const char *first=text;unsigned value=0,digits=0;
        while (*text>='0' && *text<='9') {value=value*10+(unsigned)(*text++-'0');if (++digits>3) return 0;}
        if (!digits || value>255 || (digits>1 && *first=='0')) return 0;
        address[i]=(uint8_t)value;
        if (i<3) {if (*text++!='.') return 0;}else if (*text) return 0;
    }
    memcpy(output,address,4);return 1;
}
const char *inet_ntop(int family,const void *input,char *output,socklen_t size) {
    if (family!=AF_INET) {errno=EAFNOSUPPORT;return 0;}
    if (range(input,4,0)) return 0;
    const uint8_t *p=input;char buffer[16];
    int n=snprintf(buffer,sizeof(buffer),"%u.%u.%u.%u",p[0],p[1],p[2],p[3]);
    if (size<=(unsigned)n) {errno=ENOSPC;return 0;}
    if (range(output,(size_t)n+1,1)) return 0;
    memcpy(output,buffer,(size_t)n+1);return output;
}
int socket(int family,int type,int protocol) {
    if (family!=AF_INET) {errno=EAFNOSUPPORT;return -1;}
    if ((type&~(SOCK_NONBLOCK|SOCK_CLOEXEC))!=SOCK_STREAM || (protocol && protocol!=IPPROTO_TCP)) {errno=EPROTONOSUPPORT;return -1;}
    uint32_t flags=0;if (type&SOCK_NONBLOCK) flags|=NOTOS_O_NONBLOCK;if (type&SOCK_CLOEXEC) flags|=NOTOS_O_CLOEXEC;
    return notos_checked(notos_raw_call(SYS_SOCKET,AF_INET,SOCK_STREAM,flags));
}
int connect(int fd,const struct sockaddr *address,socklen_t length) {
    if (length<sizeof(struct sockaddr_in)) {errno=EINVAL;return -1;}
    if (range(address,sizeof(struct sockaddr_in),0)) return -1;
    const struct sockaddr_in *in=(const struct sockaddr_in *)address;
    if (in->sin_family!=AF_INET) {errno=EAFNOSUPPORT;return -1;}
    struct notos_socket_address wire;memcpy(wire.ip,&in->sin_addr,4);wire.port=ntohs(in->sin_port);
    return notos_checked(notos_raw_call(SYS_CONNECT,(uint32_t)fd,(uint32_t)&wire,0));
}
int accept(int fd,struct sockaddr *address,socklen_t *length) {
    (void)fd;(void)address;(void)length;
    errno=ENOTSUP;return -1;
}
struct hostent *gethostbyname(const char *name) {
    (void)name;
    errno=ENOTSUP;return 0;
}
ssize_t send(int fd,const void *buffer,size_t bytes,int flags) {
    if (bytes>INT_MAX || (flags&~(MSG_DONTWAIT|MSG_NOSIGNAL))) {errno=EINVAL;return -1;}
    if (bytes>NOTOS_IO_LIMIT) bytes=NOTOS_IO_LIMIT;
    return notos_checked(notos_raw_call4(SYS_SEND,(uint32_t)fd,(uint32_t)buffer,bytes,(uint32_t)(flags&MSG_DONTWAIT)));
}
ssize_t recv(int fd,void *buffer,size_t bytes,int flags) {
    if (bytes>INT_MAX || (flags&~(MSG_DONTWAIT|MSG_PEEK))) {errno=EINVAL;return -1;}
    if (bytes>NOTOS_IO_LIMIT) bytes=NOTOS_IO_LIMIT;
    return notos_checked(notos_raw_call4(SYS_RECV,(uint32_t)fd,(uint32_t)buffer,bytes,(uint32_t)flags));
}
int shutdown(int fd,int how) {return notos_checked(notos_raw_call(SYS_SHUTDOWN,(uint32_t)fd,(uint32_t)how,0));}
static int option(int level,int name) {
    if (level==IPPROTO_TCP && name==TCP_NODELAY) return NOTOS_OPT_NODELAY;
    if (level==SOL_SOCKET) switch (name) {
    case SO_ERROR:return NOTOS_OPT_ERROR;
    case SO_TYPE:return NOTOS_OPT_TYPE;
    case SO_RCVTIMEO:return NOTOS_OPT_RCVTIMEO;
    case SO_SNDTIMEO:return NOTOS_OPT_SNDTIMEO;
    case SO_RCVBUF:return NOTOS_OPT_RCVBUF;
    case SO_SNDBUF:return NOTOS_OPT_SNDBUF;
    }
    errno=ENOPROTOOPT;return -1;
}
int setsockopt(int fd,int level,int name,const void *value,socklen_t length) {
    int id=option(level,name);if (id<0) return -1;
    uint32_t wire;
    if (id==NOTOS_OPT_RCVTIMEO || id==NOTOS_OPT_SNDTIMEO) {
        if (length<sizeof(struct timeval)) {errno=EINVAL;return -1;}
        if (range(value,sizeof(struct timeval),0)) return -1;
        struct timeval time;memcpy(&time,value,sizeof(time));
        if (time.tv_sec<0 || time.tv_sec>60 || time.tv_usec<0 || time.tv_usec>=1000000) {errno=EINVAL;return -1;}
        wire=(uint32_t)time.tv_sec*1000+((uint32_t)time.tv_usec+999)/1000;
        if (wire>60000) {errno=EINVAL;return -1;}
    } else {
        if (length<sizeof(int)) {errno=EINVAL;return -1;}
        if (range(value,sizeof(int),0)) return -1;
        memcpy(&wire,value,sizeof(wire));
    }
    return notos_checked(notos_raw_call(SYS_SETOPT,(uint32_t)fd,(uint32_t)id,wire));
}
int getsockopt(int fd,int level,int name,void *value,socklen_t *length) {
    int id=option(level,name);if (id<0) return -1;
    if (range(length,sizeof(*length),1)) return -1;
    socklen_t needed=(id==NOTOS_OPT_RCVTIMEO || id==NOTOS_OPT_SNDTIMEO)?sizeof(struct timeval):sizeof(int);
    if (*length<needed) {errno=EINVAL;return -1;}
    if (range(value,needed,1)) return -1;
    uint32_t wire;
    if (notos_checked(notos_raw_call(SYS_GETOPT,(uint32_t)fd,(uint32_t)id,(uint32_t)&wire))) return -1;
    if (needed==sizeof(struct timeval)) {
        struct timeval time={(time_t)(wire/1000),(suseconds_t)(wire%1000)*1000};memcpy(value,&time,needed);
    } else {int result=id==NOTOS_OPT_ERROR && wire?notos_errno((int)wire):(int)wire;memcpy(value,&result,needed);}
    *length=needed;return 0;
}
static int address_result(int fd,struct sockaddr *address,socklen_t *length,int peer) {
    if (range(length,sizeof(*length),1)) return -1;
    if (*length<sizeof(struct sockaddr_in)) {errno=EINVAL;return -1;}
    if (range(address,sizeof(struct sockaddr_in),1)) return -1;
    struct notos_socket_address wire;
    if (notos_checked(notos_raw_call(SYS_ADDRESS,(uint32_t)fd,(uint32_t)&wire,(uint32_t)peer))) return -1;
    struct sockaddr_in in={.sin_family=AF_INET,.sin_port=htons((uint16_t)wire.port)};
    memcpy(&in.sin_addr,wire.ip,4);memcpy(address,&in,sizeof(in));*length=sizeof(in);return 0;
}
int getsockname(int fd,struct sockaddr *address,socklen_t *length) {return address_result(fd,address,length,0);}
int getpeername(int fd,struct sockaddr *address,socklen_t *length) {return address_result(fd,address,length,1);}
static int resolve(const char *name,struct in_addr *address) {
    unsigned label=0;
    for (const char *p=name;*p;p++) {
        char c=*p;
        if (c=='.') {if (!label || label>63) return EAI_NONAME;label=0;}
        else if ((c>='a'&&c<='z')||(c>='A'&&c<='Z')||(c>='0'&&c<='9')||c=='-') label++;
        else return EAI_NONAME;
    }
    if (!label || label>63) return EAI_NONAME;
    int token=notos_raw_call(SYS_RESOLVE,(uint32_t)name,0,0);
    if (token<0) {errno=notos_errno(-token);return EAI_SYSTEM;}
    for (;;) {
        int result=notos_raw_call(SYS_RESOLVE_RESULT,(uint32_t)token,(uint32_t)address,0);
        if (!result) return 0;
        if (result==-ERROR_NOT_FOUND) return EAI_NONAME;
        if (result==-ERROR_TIMED_OUT) return EAI_AGAIN;
        if (result!=-ERROR_AGAIN) {errno=notos_errno(-result);return EAI_SYSTEM;}
        struct timespec delay={0,10000000};if (nanosleep(&delay,0)) return EAI_SYSTEM;
    }
}
int getaddrinfo(const char *node,const char *service,const struct addrinfo *hints,struct addrinfo **output) {
    if (range(output,sizeof(*output),1)) return EAI_SYSTEM;
    *output=0;
    if (hints && range(hints,sizeof(*hints),0)) return EAI_SYSTEM;
    int flags=hints?hints->ai_flags:0;
    if (flags&~(AI_PASSIVE|AI_NUMERICHOST|AI_NUMERICSERV)) return EAI_BADFLAGS;
    if (hints && hints->ai_family!=AF_UNSPEC && hints->ai_family!=AF_INET) return EAI_FAMILY;
    if (hints && ((hints->ai_socktype && hints->ai_socktype!=SOCK_STREAM) || (hints->ai_protocol && hints->ai_protocol!=IPPROTO_TCP))) return EAI_SOCKTYPE;
    unsigned port=0;
    if (service) {
        if (string_range(service,6)) return EAI_SERVICE;
        if (!*service) return EAI_SERVICE;
        for (const char *p=service;*p;p++) {if (*p<'0'||*p>'9') return EAI_SERVICE;port=port*10+(unsigned)(*p-'0');if (port>65535) return EAI_SERVICE;}
    }
    struct in_addr ip;
    if (!node) ip.s_addr=htonl(flags&AI_PASSIVE?INADDR_ANY:INADDR_LOOPBACK);
    else {
        if (string_range(node,96)) return EAI_NONAME;
        /* Long DNS names need not fit inet_pton's bounded numeric buffer. */
        int numeric=strlen(node)<16?inet_pton(AF_INET,node,&ip):0;
        if (numeric<0) return EAI_SYSTEM;
        if (!numeric) {if (flags&AI_NUMERICHOST) return EAI_NONAME;int error=resolve(node,&ip);if (error) return error;}
    }
    struct addrinfo *info=calloc(1,sizeof(*info));struct sockaddr_in *in=calloc(1,sizeof(*in));
    if (!info || !in) {free(info);free(in);return EAI_MEMORY;}
    *in=(struct sockaddr_in){.sin_family=AF_INET,.sin_port=htons((uint16_t)port),.sin_addr=ip};
    info->ai_flags=flags;info->ai_family=AF_INET;info->ai_socktype=SOCK_STREAM;info->ai_protocol=IPPROTO_TCP;
    info->ai_addrlen=sizeof(*in);info->ai_addr=(struct sockaddr *)in;*output=info;return 0;
}
void freeaddrinfo(struct addrinfo *info) {while (info) {struct addrinfo *next=info->ai_next;free(info->ai_addr);free(info->ai_canonname);free(info);info=next;}}
const char *gai_strerror(int error) {
    switch (error) {
    case 0:return "Success";case EAI_BADFLAGS:return "Unsupported flags";case EAI_NONAME:return "Name not found";
    case EAI_AGAIN:return "Temporary failure";case EAI_FAMILY:return "Unsupported family";
    case EAI_SOCKTYPE:return "Unsupported socket type";case EAI_SERVICE:return "Numeric service required";
    case EAI_MEMORY:return "Out of memory";case EAI_SYSTEM:return "System error";default:return "Resolver failure";
    }
}
