#define _DEFAULT_SOURCE 1
#define _POSIX_C_SOURCE 200809L
#include "os.h"
#include <errno.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdint.h>
#include <stddef.h>
#include <unistd.h>
#include <limits.h>
#include <time.h>
#include <sys/time.h>
static int constructed;
static void before_main(void) __attribute__((constructor));
static void before_main(void) {constructed=1;}
static void finishing(void) {constructed=2;puts("LIBC: atexit PASS");}
static void check(int condition,int line) {
    if (!condition) {fprintf(stderr,"LIBC FAIL: line=%d errno=%d\n",line,errno);fflush(stderr);_exit(91);}
}
#define CHECK(value) check(!!(value),__LINE__)
static void after_main(void) __attribute__((destructor));
static void after_main(void) {CHECK(constructed==2);puts("LIBC: fini-array PASS");}
static int compare(const void *a,const void *b) {
    int first=*(const int *)a,second=*(const int *)b;
    return (first>second)-(first<second);
}
static void core(void) {
    char buffer[96],*end;
    CHECK(strlen("NotOS")==5);
    memcpy(buffer,"abcdef",7);memmove(buffer+1,buffer,6);CHECK(!strcmp(buffer,"aabcdef"));
    CHECK(!strncmp(buffer,"aac",2) && strchr(buffer,'d')==buffer+4);
    memset(buffer,'q',31);CHECK(buffer[30]=='q');
    CHECK(snprintf(buffer,sizeof(buffer),"%s %d %08x %llu","NotOS",-42,0xa5u,18446744073709551615ull)==39);
    CHECK(!strcmp(buffer,"NotOS -42 000000a5 18446744073709551615"));
    /* Runtime capacity keeps intentional truncation from becoming a compile-time diagnostic. */
    volatile size_t truncation_capacity=4;
    CHECK(snprintf(buffer,truncation_capacity,"abcdef")==6 && !strcmp(buffer,"abc"));
    errno=0;CHECK(strtol("  -123tail",&end,10)==-123 && !strcmp(end,"tail") && !errno);
    CHECK(strtol("0x7f",&end,0)==127 && !*end);
    errno=0;CHECK(strtol("999999999999999999999",&end,10)==LONG_MAX && errno==ERANGE);
    CHECK(strtoll("-9223372036854775808",&end,10)==LLONG_MIN && !*end);
    int values[]={9,-3,9,0,11,-5};qsort(values,6,sizeof(int),compare);
    for (unsigned i=1;i<6;i++) CHECK(values[i-1]<=values[i]);
    int key=11;CHECK(bsearch(&key,values,6,sizeof(int),compare)==values+5);
    CHECK(!setenv("NOTOS_TEST","private",1) && !strcmp(getenv("NOTOS_TEST"),"private"));
    CHECK(!unsetenv("NOTOS_TEST") && !getenv("NOTOS_TEST"));
    char *copy=strdup("heap string");CHECK(copy && !strcmp(copy,"heap string"));free(copy);
    puts("LIBC: strings/format/strtol/qsort/env PASS");
}
static void allocations(void) {
    unsigned char *slots[32]={0};size_t sizes[32]={0};uint32_t seed=3;
    for (unsigned i=0;i<2500;i++) {
        seed=seed*1664525u+1013904223u;unsigned slot=seed%32;
        if (slots[slot]) {
            for (size_t j=0;j<sizes[slot];j++) CHECK(slots[slot][j]==(unsigned char)slot);
            free(slots[slot]);slots[slot]=0;
        } else {
            size_t size=(seed>>8)%2048+1;slots[slot]=malloc(size);CHECK(slots[slot]);sizes[slot]=size;
            CHECK((uintptr_t)slots[slot]%_Alignof(max_align_t)==0);memset(slots[slot],slot,size);
        }
    }
    for (unsigned i=0;i<32;i++) free(slots[i]);
    unsigned char *p=calloc(4097,1);CHECK(p);
    for (unsigned i=0;i<4097;i++) CHECK(p[i]==0);
    memset(p,0x39,4097);unsigned char *larger=realloc(p,16385);CHECK(larger);
    for (unsigned i=0;i<4097;i++) CHECK(larger[i]==0x39);
    p=realloc(larger,128);CHECK(p && p[127]==0x39);free(p);
    volatile size_t huge=SIZE_MAX;errno=0;CHECK(!malloc(huge) && errno==ENOMEM);
    errno=0;CHECK(!calloc(huge,2) && errno==ENOMEM);
    p=malloc(3u*1024u*1024u);CHECK(p);p[0]=0x17;p[3u*1024u*1024u-1]=0x23;
    errno=0;CHECK(!realloc(p,5u*1024u*1024u) && errno==ENOMEM);
    CHECK(p[0]==0x17 && p[3u*1024u*1024u-1]==0x23);free(p);free(0);
    void *old=sbrk(0);CHECK(sbrk(INTPTR_MIN)==(void *)-1 && errno==ENOMEM && sbrk(0)==old);
    puts("LIBC: malloc/calloc/realloc/free/OOM PASS");
}
static void platform(void) {
    char buffer[8];
    errno=0;CHECK(write(77,"x",1)==-1 && errno==EBADF);
    CHECK(write(1,0,0)==0);
    errno=0;CHECK(write(1,(void *)0x10000,1)==-1 && errno==EFAULT);
    errno=0;CHECK(read(0,buffer,1)==-1 && errno==ENOSYS);
    errno=0;CHECK(fgetc(stdin)==EOF && ferror(stdin) && errno==ENOSYS);clearerr(stdin);
    errno=0;CHECK(!fopen("/not-yet-implemented","w") && errno==ENOSYS);
    errno=0;CHECK(getentropy((void *)0x08000000,1)==-1 && errno==EFAULT);
    errno=0;CHECK(getentropy(buffer,257)==-1 && errno==EIO);
    unsigned char first[256],second[256];CHECK(!getentropy(first,sizeof(first)) && !getentropy(second,sizeof(second)));
    CHECK(memcmp(first,second,sizeof(first))!=0);
    struct timeval tv;CHECK(!gettimeofday(&tv,0) && tv.tv_sec>=946684800LL && tv.tv_usec==0);
    unsigned char unaligned[13];
    CHECK(notos_raw_call(SYS_TIME,NOTOS_CLOCK_MONOTONIC,(uint32_t)(unaligned+1),0)==0);
    struct notos_timestamp wire;memcpy(&wire,unaligned+1,sizeof(wire));
    CHECK(wire.seconds_high==0 && wire.nanoseconds<1000000000);
    struct timespec now;CHECK(!clock_gettime(CLOCK_MONOTONIC,&now) && now.tv_nsec>=0 && now.tv_nsec<1000000000);
    CHECK(clock_gettime(CLOCK_REALTIME,0)==-1 && errno==EFAULT);
    CHECK(clock_gettime((clockid_t)-1,&now)==-1 && errno==EINVAL);
    CHECK(!clock_getres(CLOCK_MONOTONIC,&now) && now.tv_sec==0 && now.tv_nsec==1000000);
    CHECK(!clock_getres(CLOCK_REALTIME,&now) && now.tv_sec==1 && now.tv_nsec==0);
    struct timespec invalid={-1,0};CHECK(nanosleep(&invalid,0)==-1 && errno==EINVAL);
    invalid=(struct timespec){0,1000000000};CHECK(nanosleep(&invalid,0)==-1 && errno==EINVAL);
    invalid=(struct timespec){60,1};CHECK(nanosleep(&invalid,0)==-1 && errno==EINVAL);
    time_t epoch=2208988800LL;struct tm utc;CHECK(gmtime_r(&epoch,&utc) && utc.tm_year==140 && utc.tm_mon==0 && utc.tm_mday==1);
    CHECK(time(0)>946684800LL && sizeof(time_t)==8);
    CHECK(getpid()>0 && isatty(1));

    puts("LIBC: errno/console/time64/calendar/entropy PASS");
}
int main(int argc,char **argv) {
    CHECK(constructed && argc==2 && argv && !strcmp(argv[0],"libc") && argv[2]==0);
    CHECK(!atexit(finishing));
    if (!strcmp(argv[1],"noentropy")) {
        char bytes[32];errno=0;CHECK(getentropy(bytes,sizeof(bytes))==-1 && errno==EIO);
        puts("LIBC: entropy unavailable FAIL-CLOSED PASS");return 0;
    }
    if (!strcmp(argv[1],"exit")) {printf("LIBC: buffered output");exit(23);}
    core();allocations();platform();
    errno=1000+(int)getpid();int marker=errno;struct timespec delay={0,200000000};
    CHECK(!nanosleep(&delay,0) && errno==marker);
    CHECK(write(2,"LIBC: stderr PASS\n",18)==18);
    puts("LIBC: process-local errno/heap PASS");
    printf("LIBC: main-return PASS"); /* No newline: exit must flush this buffer. */
    return 0;
}
