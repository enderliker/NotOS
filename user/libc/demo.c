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
#include <sys/select.h>
#include <sys/stat.h>
#include <sys/wait.h>
#include <fcntl.h>
#include <poll.h>
#include <signal.h>
#include <spawn.h>
#include <dirent.h>
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
static void multiplexing(void) {
    fd_set readers,writers,exceptions;
    struct timeval timeout={0,0};
    FD_ZERO(&readers);FD_ZERO(&writers);FD_ZERO(&exceptions);
    FD_SET(0,&readers);FD_SET(1,&writers);FD_SET(1,&exceptions);
    CHECK(select(2,&readers,&writers,&exceptions,&timeout)==1);
    CHECK(!FD_ISSET(0,&readers) && FD_ISSET(1,&writers) && !FD_ISSET(1,&exceptions));
    int file=open("/dev/null",O_RDWR);CHECK(file==3);
    FD_SET(file,&readers);FD_SET(file,&writers);
    CHECK(select(file+1,&readers,&writers,0,&timeout)==3);
    CHECK(FD_ISSET(file,&readers) && FD_ISSET(file,&writers));
    CHECK(!close(file));
    CHECK(select(file+1,&readers,&writers,0,&timeout)==-1 && errno==EBADF);
    CHECK(FD_ISSET(file,&readers) && FD_ISSET(file,&writers));
    FD_ZERO(&readers);FD_SET(0,&readers);timeout.tv_usec=1000;
    CHECK(!select(1,&readers,0,0,&timeout) && !FD_ISSET(0,&readers));
    timeout.tv_sec=-1;CHECK(select(0,0,0,0,&timeout)==-1 && errno==EINVAL);
    timeout.tv_sec=INT64_MAX;timeout.tv_usec=999999;
    FD_ZERO(&writers);FD_SET(1,&writers);CHECK(select(2,0,&writers,0,&timeout)==1);
    CHECK(select(65,0,0,0,0)==-1 && errno==EINVAL);
    CHECK(select(2,(fd_set *)0x08000000,0,0,0)==-1 && errno==EFAULT);
    CHECK(select(0,0,0,0,(struct timeval *)0x10000)==-1 && errno==EFAULT);
    CHECK(mkdir("/tmp",0700)==-1 && errno==EEXIST);
    char directory[48];CHECK(snprintf(directory,sizeof(directory),"/tmp/nested-%d",(int)getpid())>0);
    CHECK(!mkdir(directory,0700) && !rmdir(directory));
    puts("LIBC: select readiness/timeout/errors and mkdir/rmdir PASS");
}
static void allocations(int concurrent) {
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
    size_t pressure_bytes=(concurrent?2u:3u)*1024u*1024u;
    p=malloc(pressure_bytes);CHECK(p);p[0]=0x17;p[pressure_bytes-1]=0x23;
    errno=0;CHECK(!realloc(p,5u*1024u*1024u) && errno==ENOMEM);
    CHECK(p[0]==0x17 && p[pressure_bytes-1]==0x23);free(p);free(0);
    void *old=sbrk(0);CHECK(sbrk(INTPTR_MIN)==(void *)-1 && errno==ENOMEM && sbrk(0)==old);
    puts("LIBC: malloc/calloc/realloc/free/OOM PASS");
}
static void platform(void) {
    char buffer[8];
    errno=0;CHECK(write(77,"x",1)==-1 && errno==EBADF);
    CHECK(write(1,0,0)==0);
    errno=0;CHECK(write(1,(void *)0x10000,1)==-1 && errno==EFAULT);
    CHECK(!fcntl(0,F_SETFL,O_NONBLOCK));
    errno=0;CHECK(read(0,buffer,1)==-1 && errno==EAGAIN);
    errno=0;CHECK(fgetc(stdin)==EOF && ferror(stdin) && errno==EAGAIN);clearerr(stdin);
    CHECK(!fcntl(0,F_SETFL,0));
    errno=0;CHECK(!fopen("/not-yet-implemented","w") && errno==ENOENT);
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
static void files(void) {
    char path[48],buffer[32];
    CHECK(snprintf(path,sizeof(path),"/tmp/libc-%d",(int)getpid())>0);
    int file=open(path,O_CREAT|O_EXCL|O_RDWR|O_CLOEXEC,0600);CHECK(file==3);
    CHECK((fcntl(file,F_GETFD)&FD_CLOEXEC) && fcntl(file,F_GETFL)==O_RDWR);
    CHECK(open(path,O_CREAT|O_EXCL|O_RDWR,0600)==-1 && errno==EEXIST);
    CHECK(write(file,"abc\0def",7)==7 && lseek(file,0,SEEK_SET)==0);
    int alias=dup(file);CHECK(alias==4 && fcntl(alias,F_GETFD)==0);
    CHECK(read(alias,buffer,2)==2 && !memcmp(buffer,"ab",2));
    CHECK(read(file,buffer,2)==2 && buffer[0]=='c' && !buffer[1]);
    CHECK(read(file,(void *)0x08000000,1)==-1 && errno==EFAULT && lseek(alias,0,SEEK_CUR)==4);
    CHECK(fstat(file,(struct stat *)0x08000000)==-1 && errno==EFAULT);
    struct stat status;CHECK(!fstat(file,&status) && S_ISREG(status.st_mode) && status.st_size==7 && status.st_mtime>=946684800LL);
    CHECK(!isatty(file) && errno==ENOTTY);
    CHECK(!fcntl(alias,F_SETFL,O_RDWR|O_APPEND) && !lseek(file,0,SEEK_SET));
    CHECK(write(file,"!",1)==1 && lseek(alias,0,SEEK_CUR)==8);
    char moved[48];CHECK(snprintf(moved,sizeof(moved),"/tmp/moved-%d",(int)getpid())>0);
    CHECK(!rename(path,moved) && stat(path,&status)==-1 && errno==ENOENT);
    CHECK(!stat(moved,&status) && status.st_size==8);
    CHECK(rename(moved,"/disk/cross")== -1 && errno==EXDEV);
    CHECK(!rename(moved,path) && !stat(path,&status) && status.st_size==8);
    char dirpath[48],dirfile[48];
    CHECK(snprintf(dirpath,sizeof(dirpath),"/tmp/dir-%d",(int)getpid())>0);
    CHECK(snprintf(dirfile,sizeof(dirfile),"%s/item",dirpath)>0);
    CHECK(!mkdir(dirpath,0755));
    int dfd=open(dirfile,O_CREAT|O_WRONLY,0600);CHECK(dfd>=0 && !close(dfd));
    DIR *dir=opendir(dirpath);CHECK(dir && dirfd(dir)>=0);
    struct dirent *de=readdir(dir);CHECK(de && !strcmp(de->d_name,"item") && de->d_ino>0);
    CHECK(!readdir(dir));
    rewinddir(dir);
    de=readdir(dir);CHECK(de && !strcmp(de->d_name,"item"));
    CHECK(!closedir(dir) && !unlink(dirfile) && !rmdir(dirpath));
    CHECK(!unlink(path) && stat(path,&status)==-1 && errno==ENOENT);
    CHECK(!fstat(file,&status) && status.st_nlink==0);
    int replacement=open(path,O_CREAT|O_EXCL|O_RDWR,0600);CHECK(replacement==5);
    CHECK(!fstat(replacement,&status) && status.st_size==0);
    CHECK(!unlink(path) && !close(replacement) && !close(alias) && !close(file));
    CHECK(close(file)==-1 && errno==EBADF);
    FILE *stream=fopen(path,"w+");CHECK(stream);
    unsigned char *bytes=malloc(8193),*copy=malloc(8193);CHECK(bytes && copy);
    for (unsigned i=0;i<8193;i++) bytes[i]=(unsigned char)(i*31u);
    CHECK(fwrite(bytes,1,8193,stream)==8193 && !fflush(stream));
    CHECK(!fseek(stream,0,SEEK_SET) && fread(copy,1,8193,stream)==8193 && !memcmp(bytes,copy,8193));
    CHECK(fgetc(stream)==EOF && feof(stream) && !ferror(stream));
    CHECK(!fclose(stream));
    stream=fopen(path,"a");CHECK(stream && fwrite("xy",1,2,stream)==2 && !fclose(stream));
    CHECK(!stat(path,&status) && status.st_size==8195);
    file=open(path,O_RDWR);CHECK(file==3);
    CHECK(!ftruncate(file,1) && !ftruncate(file,8193) && !lseek(file,0,SEEK_SET));
    CHECK(read(file,copy,4096)==4096 && read(file,copy+4096,4097)==4096 && read(file,copy+8192,1)==1);
    for (unsigned i=0;i<8193;i++) CHECK(!copy[i]);
    CHECK(lseek(file,65536,SEEK_SET)==65536 && write(file,"x",1)==-1 && errno==EFBIG);
    CHECK(!close(file));free(bytes);free(copy);
    int full[13];
    for (unsigned i=0;i<13;i++) {full[i]=open("/dev/null",O_RDWR);CHECK(full[i]==(int)i+3);}
    CHECK(open(path,O_WRONLY|O_TRUNC)==-1 && errno==EMFILE);
    CHECK(!stat(path,&status) && status.st_size==8193);
    for (unsigned i=0;i<13;i++) CHECK(!close(full[i]));
    file=open(path,O_WRONLY);int saved=dup(1);CHECK(file==3 && saved==4);
    CHECK(!fflush(stdout) && dup2(file,1)==1 && write(1,"redirect",8)==8 && dup2(saved,1)==1);
    CHECK(!close(saved) && !close(file));
    file=open(path,O_RDONLY);CHECK(read(file,buffer,8)==8 && !memcmp(buffer,"redirect",8));
    CHECK(!close(file) && !unlink(path));
    CHECK(open((const char *)0x10000,O_RDONLY)==-1 && errno==EFAULT);
    char too_long[65];memset(too_long,'a',64);too_long[64]=0;
    CHECK(open(too_long,O_RDONLY)==-1 && errno==ENAMETOOLONG);
    uint32_t page=(uint32_t)notos_raw_call(SYS_MMAP,4096,0,0);CHECK(page>=0x08800000 && page<0x08c00000);
    memcpy((void *)(page+4093),"abc",3);
    CHECK(open((const char *)(page+4093),O_RDONLY)==-1 && errno==EFAULT);
    CHECK(!notos_raw_call(SYS_MUNMAP,page,4096,0));
    puts("VFS: binary stdio/seek/append/dup/unlink/errors/FD-limit PASS");
}
static void input_test(void) {
    char line[64];puts("VFS: stdin waiting");
    CHECK(fgets(line,sizeof(line),stdin) && !strcmp(line,"hello-vfs\n"));
    puts("VFS: stdin received");
    CHECK(fgetc(stdin)==EOF && feof(stdin) && !ferror(stdin));
    puts("VFS: blocking stdin/EOF PASS");
}
static void disk_test(void) {
    FILE *file=fopen("/disk/hello.txt","r");CHECK(file);
    puts("VFS: disk snapshot waiting");struct timespec delay={0,300000000};CHECK(!nanosleep(&delay,0));
    char data[16]={0};CHECK(fread(data,1,9,file)==9 && !memcmp(data,"new-data!",9) && !fclose(file));
    int fd=open("/disk/hello.txt",O_RDONLY);CHECK(fd==3);
    CHECK(read(fd,data,9)==9 && !memcmp(data,"new-data!",9));
    struct stat a,b;CHECK(!fstat(fd,&a) && !stat("/disk/hello.txt",&b) && a.st_ino==b.st_ino && a.st_mtime>=946684800LL && b.st_mtime==a.st_mtime);
    CHECK(!close(fd));
    CHECK(!rename("/disk/hello.txt","/disk/moved.txt") && stat("/disk/hello.txt",&b)==-1 && errno==ENOENT);
    CHECK(!stat("/disk/moved.txt",&b) && b.st_ino==a.st_ino && b.st_size==9);
    CHECK(!rename("/disk/moved.txt","/disk/hello.txt"));
    fd=open("/disk/hello.txt",O_WRONLY);CHECK(fd==3 && !close(fd));
    puts("VFS: NotFS coherent reads/writable open PASS");
}
static void curl_file(void) {
    FILE *file=fopen("/tmp/curl.bin","rb");CHECK(file);
    struct stat status;CHECK(!stat("/tmp/curl.bin",&status) && status.st_size==32768);
    unsigned char buffer[997];unsigned offset=0;size_t bytes;
    while ((bytes=fread(buffer,1,sizeof(buffer),file))) {
        for (size_t i=0;i<bytes;i++) CHECK(buffer[i]==(unsigned char)((offset+i)*17u+3u));
        offset+=(unsigned)bytes;
    }
    CHECK(offset==32768 && feof(file) && !ferror(file));
    CHECK(!fclose(file) && !unlink("/tmp/curl.bin"));
    puts("LIBC: curl binary file/content/unlink PASS");
}
static void held_file(int fault) {
    char name[48];CHECK(snprintf(name,sizeof(name),"/tmp/held-%d",(int)getpid())>0);
    int file=open(name,O_CREAT|O_RDWR,0600);CHECK(file==3 && dup(file)==4);
    CHECK(!ftruncate(file,8193) && !unlink(name));
    puts("VFS: orphan held");fflush(stdout);
    if (fault) __asm__ volatile("ud2");
    struct timespec delay={60,0};CHECK(!nanosleep(&delay,0));
}
static void exec_source(void) {
    pid_t pid = getpid();
    CHECK(!mkdir("/tmp/execdir", 0755) && !chdir("/tmp/execdir"));
    int keep = open("state", O_CREAT | O_RDWR, 0600);
    int drop = open("drop", O_CREAT | O_RDWR | O_CLOEXEC, 0600);
    CHECK(keep == 3 && drop == 4 && write(keep, &pid, sizeof(pid)) == sizeof(pid));
    CHECK(notos_exec("/tmp/missing", "") == -1 && errno == ENOENT);
    CHECK(fcntl(drop, F_GETFD) == FD_CLOEXEC);
    CHECK(notos_exec("/system/libc.elf", (const char *)0x10000) == -1 && errno == EFAULT);
    CHECK(notos_exec("state", "") == -1 && errno == ENOEXEC);
    char too_many[33];for (unsigned i=0;i<32;i++) too_many[i]=(i&1)?' ':'x';too_many[32]=0;
    CHECK(notos_exec("/system/libc.elf", too_many) == -1 && errno == EINVAL);
    int directory = open(".", O_RDONLY);CHECK(directory == 5);
    struct notos_directory_entry entry;
    CHECK(notos_readdir(directory, (struct notos_directory_entry *)0x10000) == -1 && errno == EFAULT);
    CHECK(notos_readdir(directory, &entry) == 1 && !strcmp(entry.name, "state"));
    CHECK(notos_readdir(directory, &entry) == 1 && !strcmp(entry.name, "drop"));
    CHECK(notos_readdir(directory, &entry) == 0 && !close(directory));
    CHECK(notos_exec("/system/libc.elf", "exec-target") == -1);
    CHECK(0);
}
static void exec_target(void) {
    pid_t pid = 0;
    CHECK(lseek(3, 0, SEEK_SET) == 0 && read(3, &pid, sizeof(pid)) == sizeof(pid) && pid == getpid());
    CHECK(fcntl(4, F_GETFD) == -1 && errno == EBADF);
    char cwd[64]; CHECK(getcwd(cwd, sizeof(cwd)) && !strcmp(cwd, "/tmp/execdir"));
    CHECK(!unlink("state") && !unlink("drop") && !close(3) && !chdir("/") && !rmdir("/tmp/execdir"));
    puts("EXEC: same PID, inherited offset/cwd, CLOEXEC and replacement PASS");
}
static uint32_t stream_checksum(unsigned total) {
    uint32_t hash = 2166136261u;
    for (unsigned i = 0; i < total; i++) {
        uint8_t byte = (uint8_t)(i * 73u + 19u);
        hash = (hash ^ byte) * 16777619u;
    }
    return hash;
}
static void pipe_produce(void) {
    unsigned char chunk[701];
    unsigned offset = 0, total = 16384;
    while (offset < total) {
        unsigned n = total - offset;
        if (n > sizeof(chunk)) n = sizeof(chunk);
        for (unsigned i = 0; i < n; i++) chunk[i] = (unsigned char)((offset + i) * 73u + 19u);
        ssize_t written = write(1, chunk, n);
        CHECK(written == (ssize_t)n);
        offset += n;
    }
    _exit(17);
}
static void pipe_consume(void) {
    unsigned char chunk[503];
    unsigned offset = 0;
    uint32_t hash = 2166136261u;
    for (;;) {
        ssize_t got = read(0, chunk, sizeof(chunk));
        CHECK(got >= 0);
        if (!got) break;
        for (ssize_t i = 0; i < got; i++) {
            uint8_t expected = (uint8_t)((offset + (unsigned)i) * 73u + 19u);
            CHECK(chunk[i] == expected);
            hash = (hash ^ chunk[i]) * 16777619u;
        }
        offset += (unsigned)got;
    }
    CHECK(offset == 16384);
    uint32_t summary[2] = {offset, hash};
    CHECK(write(1, summary, sizeof(summary)) == (ssize_t)sizeof(summary));
    _exit(42);
}
static void child_env(int argc, char **argv) {
    CHECK(argc == 4 && getppid() == (pid_t)atoi(argv[2]));
    CHECK(getenv("CUSTOM_ENV") && !strcmp(getenv("CUSTOM_ENV"), argv[3]));
    CHECK(getenv("SECOND") && !strcmp(getenv("SECOND"), "2") && !getenv("PIPELINE_KEY"));
    CHECK(!setenv("LOCAL_ONLY", "yes", 1) && !strcmp(getenv("LOCAL_ONLY"), "yes"));
    char cwd[64];
    CHECK(getcwd(cwd, sizeof(cwd)) && !strcmp(cwd, "/tmp/spawndir"));
    CHECK(fcntl(3, F_GETFD) == -1 && errno == EBADF);
    _exit(55);
}
static void child_orphan_maker(void) {
    char *q_argv[] = {"/system/libc.elf", "child-exit-quick", 0};
    char *l_argv[] = {"/system/libc.elf", "child-orphan-live", 0};
    struct notos_fd_map close3 = {3, NOTOS_FD_MAP_CLOSE};
    int q_pid = notos_spawn("/system/libc.elf", q_argv, 0, &close3, 1, 0);
    int l_pid = notos_spawn("/system/libc.elf", l_argv, 0, 0, 0, 0);
    CHECK(q_pid > 0 && l_pid > 0);
    struct timespec ts = {0, 40000000};
    CHECK(!nanosleep(&ts, 0));
    _exit(29);
}
static void pipeline_test(void) {
    char *bad_argv[] = {"/system/libc.elf", "child-exit-quick", 0};
    int p1[2], p2[2], status = 0;
    CHECK(notos_spawn((const char *)0x10000, bad_argv, 0, 0, 0, 0) == -1 && errno == EFAULT);
    CHECK(notos_spawn("/tmp/no-such-binary", bad_argv, 0, 0, 0, 0) == -1 && errno == ENOENT);
    CHECK(pipe((int *)0x10000) == -1 && errno == EFAULT);
    CHECK(pipe2(p1, 0x1234) == -1 && errno == EINVAL);
    CHECK(waitpid(0, &status, 0) == -1 && errno == EINVAL);
    CHECK(waitpid(-1, &status, 0x99) == -1 && errno == EINVAL);
    CHECK(kill(9999, SIGTERM) == -1 && errno == ESRCH);
    CHECK(kill(0, SIGTERM) == -1 && errno == ESRCH);
    CHECK(kill(getpid(), 99) == -1 && errno == EINVAL);
    CHECK(kill(getpid(), 0) == 0);
    /* 1. Two-stage 16 KiB checksummed pipeline (> 4096 B pipe ring buffer). */
    CHECK(!pipe2(p1, O_CLOEXEC) && !pipe2(p2, O_CLOEXEC));
    struct stat st;
    CHECK(!fstat(p1[0], &st) && S_ISFIFO(st.st_mode));
    char *prod_argv[] = {"/system/libc.elf", "pipe-produce", 0};
    struct notos_fd_map prod_map = {1, p1[1]};
    int prod_pid = notos_spawn("/system/libc.elf", prod_argv, 0, &prod_map, 1, 0);
    CHECK(prod_pid > 0);
    posix_spawn_file_actions_t actions;
    CHECK(!posix_spawn_file_actions_init(&actions));
    CHECK(!posix_spawn_file_actions_adddup2(&actions, p1[0], 0));
    CHECK(!posix_spawn_file_actions_adddup2(&actions, p2[1], 1));
    char *cons_argv[] = {"/system/libc.elf", "pipe-consume", 0};
    pid_t cons_pid = 0;
    CHECK(!posix_spawn(&cons_pid, "/system/libc.elf", &actions, 0, cons_argv, 0) && cons_pid > 0);
    CHECK(!posix_spawn_file_actions_destroy(&actions));
    CHECK(!close(p1[0]) && !close(p1[1]) && !close(p2[1]));
    uint32_t summary[2] = {0, 0};
    CHECK(read(p2[0], summary, sizeof(summary)) == (ssize_t)sizeof(summary));
    CHECK(summary[0] == 16384 && summary[1] == stream_checksum(16384));
    char eof_byte = 0;
    CHECK(read(p2[0], &eof_byte, 1) == 0 && !close(p2[0]));
    int st1 = 0, st2 = 0;
    CHECK(waitpid(prod_pid, &st1, 0) == prod_pid && WIFEXITED(st1) && WEXITSTATUS(st1) == 17);
    CHECK(waitpid(cons_pid, &st2, 0) == cons_pid && WIFEXITED(st2) && WEXITSTATUS(st2) == 42);
    CHECK(waitpid(-1, &st1, 0) == -1 && errno == ECHILD);
    /* 2. envp inheritance, custom envp, getppid, cwd inheritance, and O_CLOEXEC. */
    CHECK(!mkdir("/tmp/spawndir", 0755) && !chdir("/tmp/spawndir"));
    int clo = open("/dev/null", O_RDWR | O_CLOEXEC);
    CHECK(clo == 3);
    CHECK(!setenv("PIPELINE_KEY", "alpha_value", 1));
    char *inh_argv[] = {"/system/libc.elf", "child-inherit-env", 0};
    int inh_pid = notos_spawn("/system/libc.elf", inh_argv, 0, 0, 0, 0);
    CHECK(inh_pid > 0 && waitpid(inh_pid, &status, 0) == inh_pid && WIFEXITED(status) && WEXITSTATUS(status) == 66);
    char ppid_str[16];
    CHECK(snprintf(ppid_str, sizeof(ppid_str), "%d", (int)getpid()) > 0);
    char *env_argv[] = {"/system/libc.elf", "child-env", ppid_str, "bravo_99", 0};
    char *custom_env[] = {"CUSTOM_ENV=bravo_99", "SECOND=2", 0};
    int env_pid = notos_spawn("/system/libc.elf", env_argv, custom_env, 0, 0, 0);
    CHECK(env_pid > 0 && waitpid(env_pid, &status, 0) == env_pid && WIFEXITED(status) && WEXITSTATUS(status) == 55);
    CHECK(!getenv("LOCAL_ONLY") && !unsetenv("PIPELINE_KEY"));
    CHECK(!close(clo) && !chdir("/") && !rmdir("/tmp/spawndir"));
    /* 3. Pipe poll(), O_NONBLOCK, EPIPE, and blocking poll wakeup. */
    CHECK(!pipe2(p1, O_NONBLOCK | O_CLOEXEC));
    CHECK(read(p1[0], &eof_byte, 1) == -1 && errno == EAGAIN);
    struct pollfd pfds[2] = {{p1[0], POLLIN, 0}, {p1[1], POLLOUT, 0}};
    CHECK(poll(pfds, 2, 0) == 1 && !pfds[0].revents && pfds[1].revents == POLLOUT);
    CHECK(write(p1[1], "xyz", 3) == 3 && !close(p1[1]));
    CHECK(poll(&pfds[0], 1, 0) == 1 && (pfds[0].revents & (POLLIN | POLLHUP)) == (POLLIN | POLLHUP));
    char buf[16] = {0};
    CHECK(read(p1[0], buf, 3) == 3 && !memcmp(buf, "xyz", 3));
    CHECK(poll(&pfds[0], 1, 0) == 1 && pfds[0].revents == POLLHUP && !close(p1[0]));
    CHECK(!pipe2(p1, O_CLOEXEC) && !close(p1[0]));
    CHECK(write(p1[1], "x", 1) == -1 && errno == EPIPE);
    pfds[1].fd = p1[1]; pfds[1].events = POLLOUT; pfds[1].revents = 0;
    CHECK(poll(&pfds[1], 1, 0) == 1 && pfds[1].revents == POLLERR && !close(p1[1]));
    CHECK(!pipe2(p1, O_CLOEXEC));
    char *wake_argv[] = {"/system/libc.elf", "child-delayed-write", 0};
    struct notos_fd_map wake_map = {1, p1[1]};
    int wake_pid = notos_spawn("/system/libc.elf", wake_argv, 0, &wake_map, 1, 0);
    CHECK(wake_pid > 0 && !close(p1[1]));
    pfds[0].fd = p1[0]; pfds[0].events = POLLIN; pfds[0].revents = 0;
    CHECK(poll(&pfds[0], 1, 2000) == 1 && (pfds[0].revents & POLLIN));
    CHECK(read(p1[0], buf, 4) == 4 && !memcmp(buf, "wake", 4) && !close(p1[0]));
    CHECK(waitpid(wake_pid, &status, 0) == wake_pid && WIFEXITED(status) && WEXITSTATUS(status) == 0);
    /* 4. Zombie & orphan cleanup across parent exit. */
    CHECK(!pipe2(p1, O_CLOEXEC));
    struct notos_fd_map orphan_map = {3, p1[1]};
    char *maker_argv[] = {"/system/libc.elf", "child-orphan-maker", 0};
    int maker_pid = notos_spawn("/system/libc.elf", maker_argv, 0, &orphan_map, 1, 0);
    CHECK(maker_pid > 0 && !close(p1[1]));
    CHECK(waitpid(maker_pid, &status, 0) == maker_pid && WIFEXITED(status) && WEXITSTATUS(status) == 29);
    CHECK(read(p1[0], buf, 9) == 9 && !memcmp(buf, "orphan-ok", 9));
    CHECK(read(p1[0], buf, 1) == 0 && !close(p1[0]));
    /* 5. Kill of processes blocked on pipe read, pipe write, nanosleep, and poll. */
    CHECK(!pipe2(p1, O_CLOEXEC));
    struct notos_fd_map read_map = {0, p1[0]};
    char *blk_read_argv[] = {"/system/libc.elf", "child-block-read", 0};
    int kpid = notos_spawn("/system/libc.elf", blk_read_argv, 0, &read_map, 1, 0);
    CHECK(kpid > 0 && waitpid(kpid, &status, WNOHANG) == 0);
    struct timespec brief = {0, 30000000};
    CHECK(!nanosleep(&brief, 0) && !kill(kpid, SIGTERM));
    CHECK(waitpid(kpid, &status, 0) == kpid && WIFSIGNALED(status) && WTERMSIG(status) == SIGTERM);
    CHECK(!close(p1[0]) && !close(p1[1]));

    CHECK(!pipe2(p1, O_CLOEXEC));
    unsigned char fill[512] = {0};
    for (unsigned i = 0; i < 8; i++) CHECK(write(p1[1], fill, sizeof(fill)) == (ssize_t)sizeof(fill));
    struct notos_fd_map write_map = {1, p1[1]};
    char *blk_write_argv[] = {"/system/libc.elf", "child-block-write", 0};
    kpid = notos_spawn("/system/libc.elf", blk_write_argv, 0, &write_map, 1, 0);
    CHECK(kpid > 0 && !nanosleep(&brief, 0) && !kill(kpid, SIGKILL));
    CHECK(waitpid(kpid, &status, 0) == kpid && WIFSIGNALED(status) && WTERMSIG(status) == SIGKILL);
    CHECK(!close(p1[0]) && !close(p1[1]));

    char *blk_sleep_argv[] = {"/system/libc.elf", "child-block-sleep", 0};
    kpid = notos_spawn("/system/libc.elf", blk_sleep_argv, 0, 0, 0, 0);
    CHECK(kpid > 0 && !nanosleep(&brief, 0) && !kill(kpid, SIGINT));
    CHECK(waitpid(kpid, &status, 0) == kpid && WIFSIGNALED(status) && WTERMSIG(status) == SIGINT);

    CHECK(!pipe2(p1, O_CLOEXEC));
    read_map.parent_fd = p1[0];
    char *blk_poll_argv[] = {"/system/libc.elf", "child-block-poll", 0};
    kpid = notos_spawn("/system/libc.elf", blk_poll_argv, 0, &read_map, 1, 0);
    CHECK(kpid > 0 && !nanosleep(&brief, 0) && !kill(kpid, SIGHUP));
    CHECK(waitpid(kpid, &status, 0) == kpid && WIFSIGNALED(status) && WTERMSIG(status) == SIGHUP);
    CHECK(!close(p1[0]) && !close(p1[1]));

    char *blk_sock_argv[] = {"/system/libc.elf", "child-block-socket", 0};
    kpid = notos_spawn("/system/libc.elf", blk_sock_argv, 0, 0, 0, 0);
    CHECK(kpid > 0 && !nanosleep(&brief, 0) && !kill(kpid, SIGQUIT));
    CHECK(waitpid(kpid, &status, 0) == kpid && WIFSIGNALED(status) && WTERMSIG(status) == SIGQUIT);
    /* 6. Four-slot exhaustion returns EAGAIN and recovers cleanly. */
    CHECK(!pipe2(p1, O_CLOEXEC));
    read_map.parent_fd = p1[0];
    int slots[3];
    for (unsigned i = 0; i < 3; i++) {
        slots[i] = notos_spawn("/system/libc.elf", blk_read_argv, 0, &read_map, 1, 0);
        CHECK(slots[i] > 0);
    }
    CHECK(notos_spawn("/system/libc.elf", blk_read_argv, 0, &read_map, 1, 0) == -1 && errno == EAGAIN);
    CHECK(!close(p1[0]) && !close(p1[1]));
    for (unsigned i = 0; i < 3; i++) {
        pid_t reaped = wait(&status);
        CHECK(reaped > 0 && WIFEXITED(status) && WEXITSTATUS(status) == 0);
    }
    CHECK(wait(&status) == -1 && errno == ECHILD);
    puts("PIPELINE: spawn/wait/pipe/kill/env/orphan/slots PASS");
}
int main(int argc,char **argv) {
    CHECK(constructed && argc>=2 && argv && (!strcmp(argv[0],"libc") || !strcmp(argv[0],"libc.elf") || !strcmp(argv[0],"/system/libc.elf")) && !argv[argc]);
    if (!strcmp(argv[1],"pipe-produce")) pipe_produce();
    if (!strcmp(argv[1],"pipe-consume")) pipe_consume();
    if (!strcmp(argv[1],"child-env")) child_env(argc, argv);
    if (!strcmp(argv[1],"child-inherit-env")) {
        CHECK(getenv("PIPELINE_KEY") && !strcmp(getenv("PIPELINE_KEY"), "alpha_value"));
        _exit(66);
    }
    if (!strcmp(argv[1],"child-exit-quick")) _exit(33);
    if (!strcmp(argv[1],"child-orphan-live")) {
        struct timespec ts = {0, 120000000};
        CHECK(!nanosleep(&ts, 0) && getppid() == 0);
        CHECK(write(3, "orphan-ok", 9) == 9);
        _exit(0);
    }
    if (!strcmp(argv[1],"child-orphan-maker")) child_orphan_maker();
    if (!strcmp(argv[1],"child-delayed-write")) {
        struct timespec ts = {0, 50000000};
        CHECK(!nanosleep(&ts, 0) && write(1, "wake", 4) == 4);
        _exit(0);
    }
    if (!strcmp(argv[1],"child-block-read")) {
        char c;
        ssize_t n = read(0, &c, 1);
        _exit(n == 0 ? 0 : 99);
    }
    if (!strcmp(argv[1],"child-block-write")) {
        ssize_t n = write(1, "x", 1);
        _exit(n == 1 ? 0 : 99);
    }
    if (!strcmp(argv[1],"child-block-sleep")) {
        struct timespec ts = {60, 0};
        nanosleep(&ts, 0);
        _exit(99);
    }
    if (!strcmp(argv[1],"child-block-poll")) {
        struct pollfd pfd = {0, POLLIN, 0};
        poll(&pfd, 1, 60000);
        _exit(99);
    }
    if (!strcmp(argv[1],"child-block-socket")) {
        int s = notos_raw_call(SYS_SOCKET, 2u, 1u, 0);
        CHECK(s >= 0);
        struct { uint8_t ip[4]; uint32_t port; } ep = {{10, 0, 2, 99}, 6543};
        notos_raw_call(SYS_CONNECT, (uint32_t)s, (uint32_t)&ep, sizeof(ep));
        _exit(99);
    }
    CHECK(argc==2);
    CHECK(!atexit(finishing));
    if (!strcmp(argv[1],"pipeline")) {pipeline_test();return 0;}
    if (!strcmp(argv[1],"exec-source")) {exec_source();return 0;}
    if (!strcmp(argv[1],"exec-target")) {exec_target();return 0;}
    if (!strcmp(argv[1],"noentropy")) {
        char bytes[32];errno=0;CHECK(getentropy(bytes,sizeof(bytes))==-1 && errno==EIO);
        puts("LIBC: entropy unavailable FAIL-CLOSED PASS");return 0;
    }
    if (!strcmp(argv[1],"exit")) {printf("LIBC: buffered output");exit(23);}
    if (!strcmp(argv[1],"curl-file")) {curl_file();return 0;}
    if (!strcmp(argv[1],"files")) {files();return 0;}
    if (!strcmp(argv[1],"stdin")) {input_test();return 0;}
    if (!strcmp(argv[1],"disk")) {disk_test();return 0;}
    if (!strcmp(argv[1],"hold") || !strcmp(argv[1],"fault")) {held_file(!strcmp(argv[1],"fault"));return 0;}
    core();allocations(!strcmp(argv[1],"concurrent"));platform();multiplexing();
    errno=1000+(int)getpid();int marker=errno;struct timespec delay={0,200000000};
    CHECK(!nanosleep(&delay,0) && errno==marker);
    CHECK(write(2,"LIBC: stderr PASS\n",18)==18);
    puts("LIBC: process-local errno/heap PASS");
    printf("LIBC: main-return PASS"); /* No newline: exit must flush this buffer. */
    return 0;
}
