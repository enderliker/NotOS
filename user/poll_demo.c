/* Readiness integration tests run in an independently loaded ring-3 ELF. */
#include "poll_demo.h"
#include "libc/os.h"
#include <poll.h>
#include <netdb.h>
#include <arpa/inet.h>
#include <sys/socket.h>
#include <time.h>
#include <unistd.h>
#include <fcntl.h>
#include <errno.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
static void require(int ok,int line) {
    if (!ok) {fprintf(stderr,"POLL FAIL: line=%d errno=%d\n",line,errno);fflush(stderr);_exit(91);}
}
#define CHECK(x) require(!!(x),__LINE__)
static unsigned milliseconds(void) {
    struct timespec t;CHECK(!clock_gettime(CLOCK_MONOTONIC,&t));return (unsigned)t.tv_sec*1000+(unsigned)t.tv_nsec/1000000;
}
static void basic(void) {
    struct pollfd fds[]={{1,POLLOUT,99},{2,POLLIN,99},{-1,-1,99},{99,0,99},{1,0,99},{1,POLLOUT,99}};
    CHECK(poll(fds,6,0)==3);
    CHECK(fds[0].revents==POLLOUT && !fds[1].revents && !fds[2].revents && fds[3].revents==POLLNVAL
        && !fds[4].revents && fds[5].revents==POLLOUT);
    CHECK(fds[2].fd==-1 && fds[3].events==0);
    fds[0]=(struct pollfd){1,0x4000,99};CHECK(!poll(fds,1,0) && !fds[0].revents);
    CHECK(poll((struct pollfd *)0x10000,1,0)==-1 && errno==EFAULT);
    CHECK(poll((struct pollfd *)0x08000000,1,0)==-1 && errno==EFAULT);
    CHECK(poll(fds,65,0)==-1 && errno==EINVAL);
    CHECK(notos_raw_call(SYS_POLL,0x10000,65,0)==-ERROR_INVALID_ARGUMENT);
    CHECK(notos_raw_call(SYS_POLL,0x10000,1,0)==-ERROR_BAD_ADDRESS);
    CHECK(notos_raw_call(SYS_POLL,0x08000000,1,0)==-ERROR_BAD_ADDRESS);
    CHECK(!poll(0,0,0));unsigned before=milliseconds();CHECK(!poll(0,0,40));CHECK(milliseconds()-before>=40);
    struct pollfd full[64];for (unsigned i=0;i<64;i++) full[i]=(struct pollfd){-1,-1,99};
    CHECK(!poll(full,64,0));for (unsigned i=0;i<64;i++) CHECK(!full[i].revents);
    /* Raw wire vector can cross an alignment boundary; kernel uses bytewise copy. */
    unsigned char unaligned[9];struct pollfd wire={1,POLLOUT,99};memcpy(unaligned+1,&wire,8);
    CHECK(notos_raw_call(SYS_POLL,(unsigned)unaligned+1,1,0)==1);memcpy(&wire,unaligned+1,8);CHECK(wire.revents==POLLOUT);
    char path[64];CHECK(snprintf(path,sizeof(path),"/tmp/poll-%d",(int)getpid())>0);
    int file=open(path,O_CREAT|O_RDWR,0600);CHECK(file>=3);CHECK(write(file,"x",1)==1);
    struct pollfd regular={file,POLLIN|POLLOUT,99};CHECK(poll(&regular,1,0)==1 && regular.revents==(POLLIN|POLLOUT));
    CHECK(lseek(file,0,SEEK_CUR)==1);char byte;CHECK(read(file,&byte,1)==0);
    CHECK(poll(&regular,1,0)==1 && regular.revents==(POLLIN|POLLOUT));CHECK(!close(file));CHECK(!unlink(path));
    int null=open("/dev/null",O_RDWR);CHECK(null>=3);regular=(struct pollfd){null,POLLIN|POLLOUT,0};
    CHECK(poll(&regular,1,0)==1 && regular.revents==(POLLIN|POLLOUT));CHECK(!close(null));
}
static void input(void) {
    struct pollfd fds[]={{0,POLLIN,99},{-1,POLLOUT,99}};
    printf("POLL: stdin waiting\n");fflush(stdout);
    CHECK(poll(fds,2,-1)==1 && fds[0].revents==POLLIN && !fds[1].revents);
    CHECK(poll(fds,2,0)==1);char text[32];CHECK(read(0,text,sizeof(text))>0);CHECK(!poll(fds,2,0));
    printf("POLL: stdin consumed\n");fflush(stdout);
    CHECK(poll(fds,2,-1)==1 && fds[0].revents==(POLLIN|POLLHUP));CHECK(read(0,text,1)==0);
}
static struct addrinfo *address(const char *service) {
    struct addrinfo hints={.ai_family=AF_INET,.ai_socktype=SOCK_STREAM},*peer=0;
    CHECK(!getaddrinfo("10.0.2.2",service,&hints,&peer));return peer;
}
static int open_connection(struct addrinfo *peer) {
    int fd=socket(AF_INET,SOCK_STREAM|SOCK_NONBLOCK,0);CHECK(fd>=3);
    CHECK(connect(fd,peer->ai_addr,peer->ai_addrlen)==-1 && errno==EINPROGRESS);return fd;
}
static void connected(int fd) {
    struct pollfd p={fd,POLLOUT,0};CHECK(poll(&p,1,3000)==1 && (p.revents&POLLOUT) && !(p.revents&POLLERR));
    int error=-1;socklen_t size=sizeof(error);CHECK(!getsockopt(fd,SOL_SOCKET,SO_ERROR,&error,&size) && !error);
}
static void finished(int fd) {
    struct pollfd p={fd,POLLIN|POLLRDHUP,0};
    CHECK(poll(&p,1,3000)==1 && (p.revents&POLLIN) && (p.revents&POLLRDHUP) && (p.revents&POLLHUP));
    char byte;CHECK(recv(fd,&byte,1,MSG_DONTWAIT)==0);
    p.events=0;CHECK(poll(&p,1,0)==1 && (p.revents&POLLHUP));
    p.events=POLLOUT;CHECK(poll(&p,1,0)==1 && (p.revents&POLLOUT));
    CHECK(send(fd,"x",1,0)==-1 && errno==EPIPE);CHECK(!close(fd));
}
static void sockets(const char *service) {
    struct addrinfo *peer=address(service);int first=open_connection(peer),second=open_connection(peer);
    struct pollfd connect_events[]={{first,POLLOUT,0},{second,POLLOUT,0}};
    CHECK(poll(connect_events,2,3000)>0);connected(first);connected(second);
    CHECK(send(first,"a",1,0)==1 && send(second,"b",1,0)==1);
    struct pollfd reads[]={{first,POLLIN,99},{second,POLLIN,99},{0,POLLIN,99},{-1,-1,99}};
    CHECK(!poll(reads,4,20));CHECK(!reads[0].revents && !reads[1].revents);
    printf("POLL: sockets waiting\n");fflush(stdout);unsigned received=0;
    while (received<2) {
        CHECK(poll(reads,4,3000)>0);
        for (unsigned i=0;i<2;i++) if (reads[i].revents) {
            CHECK(reads[i].revents==POLLIN);
            struct pollfd duplicate[]={{reads[i].fd,POLLIN,0},{reads[i].fd,POLLIN,0}};
            CHECK(poll(duplicate,2,0)==2);
            char byte;CHECK(recv(reads[i].fd,&byte,1,MSG_PEEK)==1 && byte==(i?'b':'a'));
            CHECK(poll(duplicate,2,0)==2);CHECK(read(reads[i].fd,&byte,1)==1 && byte==(i?'b':'a'));
            CHECK(!poll(duplicate,2,0));reads[i].fd=-1;received++;
        }
    }
    CHECK(!shutdown(first,SHUT_WR) && !shutdown(second,SHUT_WR));finished(first);finished(second);freeaddrinfo(peer);
}
static void failure(const char *service,int reset) {
    struct addrinfo *peer=address(service);int fd=open_connection(peer);freeaddrinfo(peer);
    if (reset) {connected(fd);CHECK(send(fd,"reset",5,0)==5);}
    struct pollfd p={fd,0,99};CHECK(poll(&p,1,3000)==1);
    CHECK((p.revents&(POLLERR|POLLHUP))==(POLLERR|POLLHUP));
    CHECK(poll(&p,1,0)==1 && (p.revents&POLLERR));
    int error=0;socklen_t size=sizeof(error);
    CHECK(!getsockopt(fd,SOL_SOCKET,SO_ERROR,&error,&size));CHECK(error==(reset?ECONNRESET:ECONNREFUSED));
    CHECK(poll(&p,1,0)==1 && (p.revents&POLLHUP) && !(p.revents&POLLERR));
    CHECK(!getsockopt(fd,SOL_SOCKET,SO_ERROR,&error,&size) && !error);CHECK(!close(fd));
}
static void backpressure(const char *service) {
    struct addrinfo *peer=address(service);int fd=open_connection(peer);freeaddrinfo(peer);connected(fd);
    unsigned char buffer[4096];memset(buffer,0xa5,sizeof(buffer));unsigned total=0;
    for (unsigned i=0;i<512;i++) {
        int n=(int)send(fd,buffer,sizeof(buffer),0);
        if (n==-1) {CHECK(errno==EAGAIN);break;}
        CHECK(n>0);total+=(unsigned)n;
    }
    CHECK(total>0 && total<512*sizeof(buffer));
    struct pollfd p={fd,POLLOUT,0};CHECK(poll(&p,1,3000)==1 && (p.revents&POLLOUT) && !(p.revents&POLLERR));
    CHECK(send(fd,"d",1,0)==1);CHECK(!shutdown(fd,SHUT_WR));finished(fd);
}
static void web(const char *service) {
    struct addrinfo hints={.ai_family=AF_INET,.ai_socktype=SOCK_STREAM},*peer=0;
    CHECK(!getaddrinfo("example.com",service,&hints,&peer));
    int fd=open_connection(peer);freeaddrinfo(peer);connected(fd);
    const char request[]="GET / HTTP/1.0\r\nHost: example.com\r\nConnection: close\r\n\r\n";
    unsigned written=0;
    while (written<sizeof(request)-1) {
        int n=(int)send(fd,request+written,sizeof(request)-1-written,0);
        if (n==-1 && errno==EAGAIN) {
            struct pollfd p={fd,POLLOUT,0};CHECK(poll(&p,1,5000)==1 && (p.revents&POLLOUT));continue;
        }
        CHECK(n>0);written+=(unsigned)n;
    }
    char response[8192];unsigned used=0;
    for (;;) {
        struct pollfd p={fd,POLLIN,0};CHECK(poll(&p,1,5000)==1 && !(p.revents&POLLERR));
        int n=(int)recv(fd,response+used,sizeof(response)-1-used,0);
        if (n==-1 && errno==EAGAIN) continue;
        CHECK(n>=0);if (!n) break;used+=(unsigned)n;CHECK(used<sizeof(response)-1);
    }
    response[used]=0;CHECK(used>12 && (!strncmp(response,"HTTP/1.0 200",12) || !strncmp(response,"HTTP/1.1 200",12)));
    CHECK(strstr(response,"\r\n\r\n"));CHECK(!close(fd));
}
int poll_demo(const char *mode,const char *service) {
    if (!strcmp(mode,"poll-basic")) basic();
    else if (!strcmp(mode,"poll-stdin")) input();
    else if (!strcmp(mode,"poll-wait")) {printf("POLL: infinite waiting\n");fflush(stdout);poll(0,0,-7);CHECK(0);}
    else if (!strcmp(mode,"poll-timer")) {printf("POLL: timer waiting\n");fflush(stdout);CHECK(!poll(0,0,600));}
    else if (!strcmp(mode,"poll-fault")) {CHECK(!poll(0,0,10));__asm__ volatile("ud2");}
    else if (!strcmp(mode,"poll")) sockets(service);
    else if (!strcmp(mode,"poll-web")) web(service);
    else if (!strcmp(mode,"poll-refused")) failure(service,0);
    else if (!strcmp(mode,"poll-reset")) failure(service,1);
    else if (!strcmp(mode,"poll-write")) backpressure(service);
    else CHECK(0);
    printf("POLL: %s PASS\n",mode);fflush(stdout);return 0;
}
