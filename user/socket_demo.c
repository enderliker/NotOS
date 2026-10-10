/* Real ring-3 integration checks; every byte crosses the NIC and TCP stack. */
#include "libc/os.h"
#include "poll_demo.h"
#include <arpa/inet.h>
#include <netinet/tcp.h>
#include <netdb.h>
#include <sys/time.h>
#include <sys/stat.h>
#include <unistd.h>
#include <fcntl.h>
#include <errno.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#define BODY_BYTES 32768u
static void check(int ok,int line) {
    if (!ok) {fprintf(stderr,"SOCKET FAIL: line=%d errno=%d\n",line,errno);fflush(stderr);_exit(90);}
}
#define CHECK(x) check(!!(x),__LINE__)
static void pause_ms(unsigned ms) {struct timespec t={ms/1000,(long)(ms%1000)*1000000};CHECK(!nanosleep(&t,0));}
static void send_all(int fd,const void *data,size_t bytes) {
    const char *p=data;
    while (bytes) {ssize_t n=send(fd,p,bytes,MSG_NOSIGNAL);CHECK(n>0);p+=n;bytes-=(size_t)n;}
}
static void pass(const char *mode) {printf("SOCKET: %s PASS\n",mode);fflush(stdout);}
static int connected_socket(const struct addrinfo *peer) {
    int fd=socket(AF_INET,SOCK_STREAM|SOCK_CLOEXEC,0);CHECK(fd>=3);
    CHECK(!connect(fd,peer->ai_addr,peer->ai_addrlen));return fd;
}
static void small_exchange(int fd) {
    struct stat status;CHECK(!fstat(fd,&status) && S_ISSOCK(status.st_mode));
    CHECK(write(fd,"ping",4)==4);CHECK(!shutdown(fd,SHUT_WR));char reply[4];size_t used=0;
    while (used<4) {ssize_t n=read(fd,reply+used,4-used);CHECK(n>0);used+=(size_t)n;}
    CHECK(!memcmp(reply,"ping",4));CHECK(recv(fd,reply,1,0)==0);
}
int main(int argc,char **argv) {
    CHECK(argc==2);char *separator=strchr(argv[1],' ');CHECK(separator);*separator++=0;
    const char *mode=argv[1];unsigned port=(unsigned)strtoul(separator,0,10);CHECK(port>0 && port<=65535);
    if (!strncmp(mode,"poll",4)) return poll_demo(mode,separator);
    struct in_addr numeric;CHECK(inet_pton(AF_INET,"10.0.2.2",&numeric)==1);
    struct in_addr preserved=numeric;CHECK(inet_pton(AF_INET,"256.0.2.2",&numeric)==0);
    CHECK(inet_pton(AF_INET,"010.0.2.2",&numeric)==0);CHECK(numeric.s_addr==preserved.s_addr);
    char rendered[16];CHECK(inet_ntop(AF_INET,&numeric,rendered,sizeof(rendered)));CHECK(!strcmp(rendered,"10.0.2.2"));
    if (!strcmp(mode,"unavailable")) {
        CHECK(socket(AF_INET,SOCK_STREAM,0)==-1);CHECK(errno==EIO || errno==ENETUNREACH);pass(mode);return 0;
    }
    if (!strcmp(mode,"quota")) {
        int fds[8];for (unsigned i=0;i<8;i++) {fds[i]=socket(AF_INET,SOCK_STREAM,0);CHECK(fds[i]==(int)i+3);}
        CHECK(socket(AF_INET,SOCK_STREAM,0)==-1 && errno==EMFILE);
        int alias=dup(fds[0]);CHECK(alias==11);CHECK(!close(fds[0]));int type=0;socklen_t n=sizeof(type);
        CHECK(!getsockopt(alias,SOL_SOCKET,SO_TYPE,&type,&n) && type==SOCK_STREAM);
        CHECK(!close(alias));for (unsigned i=1;i<8;i++) CHECK(!close(fds[i]));pass(mode);return 0;
    }
    CHECK(socket(AF_INET6,SOCK_STREAM,0)==-1 && errno==EAFNOSUPPORT);
    CHECK(socket(AF_INET,SOCK_DGRAM,0)==-1 && errno==EPROTONOSUPPORT);
    struct addrinfo hints={.ai_family=AF_INET,.ai_socktype=SOCK_STREAM},*peer=0;
    const char *host=!strcmp(mode,"dns")?"echo.socket.test":!strcmp(mode,"dnswait")?"slow.socket.test":!strcmp(mode,"web")?"example.com":"10.0.2.2";
    printf("SOCKET: resolving %s\n",host);fflush(stdout);
    CHECK(!getaddrinfo(host,separator,&hints,&peer));
    if (!strcmp(mode,"dns")) {
        struct addrinfo *missing=0;CHECK(getaddrinfo("missing.socket.test",separator,&hints,&missing)==EAI_NONAME && !missing);
    }
    if (!strcmp(mode,"pool")) {
        int held[8];for (unsigned i=0;i<8;i++) held[i]=connected_socket(peer);
        CHECK(!close(held[0]));
        /* The peer withholds its FIN: eight PCBs remain, but only seven FDs. */
        CHECK(socket(AF_INET,SOCK_STREAM,0)==-1 && errno==ENOMEM);
        for (unsigned i=1;i<8;i++) {
            struct sockaddr_in address;socklen_t n=sizeof(address);
            CHECK(!getpeername(held[i],(struct sockaddr *)&address,&n));
            send_all(held[i],"p",1);CHECK(!close(held[i]));
        }
        freeaddrinfo(peer);pass(mode);return 0;
    }
    int nonblock=!strcmp(mode,"nonblock");
    int fd=socket(AF_INET,SOCK_STREAM|SOCK_CLOEXEC|(nonblock?SOCK_NONBLOCK:0),0);CHECK(fd>=3);
    CHECK(fcntl(fd,F_GETFD)==FD_CLOEXEC);
    int one=1;CHECK(!setsockopt(fd,IPPROTO_TCP,TCP_NODELAY,&one,sizeof(one)));
    CHECK(connect(fd,(const struct sockaddr *)0x10000,sizeof(struct sockaddr_in))==-1 && errno==EFAULT);
    int result=connect(fd,peer->ai_addr,peer->ai_addrlen);
    if (!strcmp(mode,"refused")) {
        CHECK(result==-1 && errno==ECONNREFUSED);int error=0;socklen_t size=sizeof(error);
        CHECK(!getsockopt(fd,SOL_SOCKET,SO_ERROR,&error,&size) && error==ECONNREFUSED);
        CHECK(!getsockopt(fd,SOL_SOCKET,SO_ERROR,&error,&size) && error==0);
        CHECK(!close(fd));freeaddrinfo(peer);pass(mode);return 0;
    }
    if (nonblock) {
        CHECK(result==-1 && errno==EINPROGRESS);struct sockaddr_in addr;socklen_t n=sizeof(addr);unsigned tries=0;
        while (getpeername(fd,(struct sockaddr *)&addr,&n)) {CHECK(errno==ENOTCONN && tries++<500);pause_ms(10);}
        int error=-1;n=sizeof(error);CHECK(!getsockopt(fd,SOL_SOCKET,SO_ERROR,&error,&n) && !error);
        CHECK(!fcntl(fd,F_SETFL,0));
    } else CHECK(result==0);
    struct sockaddr_in local,remote;socklen_t size=sizeof(local);
    CHECK(!getsockname(fd,(struct sockaddr *)&local,&size));size=sizeof(remote);
    CHECK(!getpeername(fd,(struct sockaddr *)&remote,&size));CHECK(ntohs(remote.sin_port)==port);
    unsigned local_port=ntohs(local.sin_port);CHECK(local_port>=52000 && local_port<61000);
    CHECK(recv(fd,(void *)0x08000000,1,0)==-1 && errno==EFAULT);
    if (!strcmp(mode,"timeout")) {
        struct timeval timeout={0,200000};CHECK(!setsockopt(fd,SOL_SOCKET,SO_RCVTIMEO,&timeout,sizeof(timeout)));
        char c;CHECK(recv(fd,&c,1,0)==-1 && errno==EAGAIN);CHECK(!close(fd));freeaddrinfo(peer);pass(mode);return 0;
    }
    if (!strcmp(mode,"hold") || !strcmp(mode,"reset")) {
        if (!strcmp(mode,"reset")) send_all(fd,"reset",5);
        printf("SOCKET: receive waiting\n");fflush(stdout);char c;
        result=(int)recv(fd,&c,1,0);
        if (result!=-1 || errno!=ECONNRESET) {printf("SOCKET: reset result=%d errno=%d\n",result,errno);fflush(stdout);}
        CHECK(!strcmp(mode,"reset") && result==-1 && errno==ECONNRESET);
        CHECK(!close(fd));freeaddrinfo(peer);pass(mode);return 0;
    }
    if (!strcmp(mode,"dns") || !strcmp(mode,"dnswait")) {
        small_exchange(fd);CHECK(!close(fd));freeaddrinfo(peer);pass(mode);return 0;
    }
    int web=!strcmp(mode,"web"),http=!strcmp(mode,"http");
    unsigned char *body=malloc(BODY_BYTES),*reply=malloc(40961);CHECK(body && reply);
    for (unsigned i=0;i<BODY_BYTES;i++) body[i]=(unsigned char)(i*17+3);
    if (http) {const char request[]="GET /socket HTTP/1.0\r\nHost: 10.0.2.2\r\n\r\n";send_all(fd,request,sizeof(request)-1);}
    else if (web) {const char request[]="GET / HTTP/1.0\r\nHost: example.com\r\nConnection: close\r\n\r\n";send_all(fd,request,sizeof(request)-1);}
    else send_all(fd,body,BODY_BYTES);
    /* HTTP uses Connection: close, not an early transport half-close. */
    if (!web) {CHECK(!shutdown(fd,SHUT_WR));CHECK(send(fd,"x",1,0)==-1 && errno==EPIPE);}
    unsigned used=0;char peek[16];
    ssize_t n=recv(fd,peek,sizeof(peek),MSG_PEEK);CHECK(n>0);
    for (;;) {n=recv(fd,reply+used,40960-used,0);CHECK(n>=0);if (!n) break;used+=(unsigned)n;CHECK(used<40960);}
    reply[used]=0;
    if (http || web) {
        CHECK(used>12 && (!memcmp(reply,"HTTP/1.0 200",12) || !memcmp(reply,"HTTP/1.1 200",12)));
        char *payload=strstr((char *)reply,"\r\n\r\n");CHECK(payload);payload+=4;
        if (http) {
            CHECK(used-(unsigned)(payload-(char *)reply)==BODY_BYTES);CHECK(!memcmp(payload,body,BODY_BYTES));
            char path[64];CHECK(snprintf(path,sizeof(path),"/tmp/http-%d",(int)getpid())>0);
            FILE *file=fopen(path,"wb");CHECK(file);CHECK(fwrite(payload,1,BODY_BYTES,file)==BODY_BYTES);CHECK(!fclose(file));
            struct stat status;CHECK(!stat(path,&status) && status.st_size==BODY_BYTES);CHECK(!unlink(path));
        }
    } else {CHECK(used==BODY_BYTES);CHECK(!memcmp(reply,body,BODY_BYTES));CHECK(!memcmp(peek,reply,16));}
    if (!strcmp(mode,"lifetime")) {
        for (unsigned i=0;i<10;i++) {int next=connected_socket(peer);small_exchange(next);CHECK(!close(next));}
        size=sizeof(local);CHECK(!getsockname(fd,(struct sockaddr *)&local,&size));CHECK(ntohs(local.sin_port)==local_port);
        int value=0;size=sizeof(value);CHECK(!getsockopt(fd,IPPROTO_TCP,TCP_NODELAY,&value,&size) && value==1);
    }
    CHECK(!close(fd));freeaddrinfo(peer);free(body);free(reply);pass(mode);return 0;
}
