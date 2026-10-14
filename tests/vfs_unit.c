/* Host-only fake frame allocator. Low static addresses, never a guest libc dependency. */
#include <assert.h>
#include <stdio.h>
#include <string.h>
#include "../kernel/fd.h"
#include "../kernel/vfs.h"
#include "../kernel/platform_tls.h"
#include "../kernel/syscall_numbers.h"
static unsigned char pages[64][4096] __attribute__((aligned(4096)));
static int owned[64],fail_after=-1;
int rtc_utc(struct utc_time *out) {*out=(struct utc_time){719528u+20089u,0};return 1;}
u32 physical_alloc(void) {
    if (!fail_after) return 0;
    if (fail_after>0) fail_after--;
    for (unsigned i=0;i<64;i++) if (!owned[i]) {owned[i]=1;memset(pages[i],0xa5,4096);return (u32)(unsigned long)pages[i];}
    return 0;
}
int physical_free(u32 address) {
    for (unsigned i=0;i<64;i++) if (address==(u32)(unsigned long)pages[i]) {assert(owned[i]);owned[i]=0;return 1;}
    assert(0);return 0;
}
int fs_ready(void) {return 1;}
int fs_file_inode(const char *name) {return !strcmp(name,"sample")?1:0;}
int fs_file_size(const char *name) {return !strcmp(name,"sample")?6:-1;}
int fs_read_file(const char *name,void *buffer,u32 size) {assert(!strcmp(name,"sample")&&size>=6);memcpy(buffer,"disk\0!",6);return 6;}
void console_putc(char c) {(void)c;}
void console_puts(const char *s) {(void)s;}
void console_uint(u32 n) {(void)n;}
static void empty(void) {struct vfs_counts c=vfs_counts();assert(!c.nodes&&!c.pages&&!c.files&&!c.references&&!fd_description_count()&&!fd_pipe_count());}
static void test_pipes_and_spawn_fds(struct fd_table *a, struct fd_table *b) {
    int pfd[2];
    fail_after=0;
    assert(fd_pipe(a,pfd,0)==-ERROR_MEMORY && !fd_pipe_count());
    fail_after=-1;
    assert(fd_pipe(a,pfd,0)==0 && pfd[0]==3 && pfd[1]==4 && fd_pipe_count()==1);
    struct notos_file_info info;
    assert(fd_stat(a,pfd[0],&info)==0 && info.kind==NOTOS_FILE_FIFO && info.permissions==0600);
    assert(fd_seek(a,pfd[0],0,NOTOS_SEEK_SET)==-ERROR_ILLEGAL_SEEK);
    char scratch[4096],verify[4096];
    for (unsigned i=0;i<sizeof(scratch);i++) scratch[i]=(char)(i*13u+7u);
    assert(fd_read(a,pfd[0],verify,1)==-ERROR_AGAIN);
    assert(fd_blocking_input(a,pfd[0])==1 && fd_blocking_output(a,pfd[1])==1);
    assert(fd_blocking_input(a,pfd[1])==0 && fd_blocking_output(a,pfd[0])==0);
    struct notos_pollfd pev[2]={{pfd[0],NOTOS_POLLIN,0},{pfd[1],NOTOS_POLLOUT,0}};
    assert(fd_poll(a,pev,2)==1 && !pev[0].revents && pev[1].revents==NOTOS_POLLOUT);
    assert(fd_write(a,pfd[1],scratch,3000)==3000);
    assert(fd_write(a,pfd[1],scratch+3000,2000)==1096);
    assert(fd_write(a,pfd[1],"x",1)==-ERROR_AGAIN);
    assert(fd_read(a,pfd[0],verify,2000)==2000 && !memcmp(verify,scratch,2000));
    /* Wrap around the 4096-byte ring buffer and verify all bytes. */
    assert(fd_write(a,pfd[1],scratch,2000)==2000);
    assert(fd_read(a,pfd[0],verify,2096)==2096 && !memcmp(verify,scratch+2000,2096));
    assert(fd_read(a,pfd[0],verify,2000)==2000 && !memcmp(verify,scratch,2000));
    assert(fd_close(a,pfd[1])==0);
    assert(fd_read(a,pfd[0],verify,1)==0);
    assert(fd_poll(a,pev,1)==1 && pev[0].revents==NOTOS_POLLHUP);
    assert(fd_close(a,pfd[0])==0 && !fd_pipe_count());
    assert(fd_pipe(a,pfd,NOTOS_O_NONBLOCK)==0 && !fd_blocking_input(a,pfd[0]) && !fd_blocking_output(a,pfd[1]));
    assert(fd_close(a,pfd[0])==0);
    assert(fd_write(a,pfd[1],"x",1)==-ERROR_BROKEN_PIPE);
    pev[1].revents=0;
    assert(fd_poll(a,&pev[1],1)==1 && pev[1].revents==NOTOS_POLLERR);
    assert(fd_close(a,pfd[1])==0 && !fd_pipe_count());
    /* Exercise fd_table_spawn with CLOEXEC, explicit redirection, swap, and close. */
    assert(fd_pipe(a,pfd,NOTOS_O_CLOEXEC)==0 && pfd[0]==3 && pfd[1]==4);
    int dev=fd_open(a,"/dev/null",NOTOS_O_RDWR,0);assert(dev==5);
    struct notos_fd_map bad_map={0,9};
    struct fd_table child;
    assert(fd_table_spawn(&child,a,&bad_map,1)==-ERROR_BAD_FD);
    struct notos_fd_map map[4]={{0,pfd[0]},{3,pfd[1]},{4,3},{5,NOTOS_FD_MAP_CLOSE}};
    assert(fd_table_spawn(&child,a,map,4)==0);
    assert(fd_control(&child,0,NOTOS_F_GETFD,0)==0 && fd_control(&child,3,NOTOS_F_GETFD,0)==0);
    assert(fd_control(&child,5,NOTOS_F_GETFD,0)==-ERROR_BAD_FD);
    assert(fd_write(&child,3,"pipe-ok",7)==7 && fd_read(&child,4,verify,7)==7 && !memcmp(verify,"pipe-ok",7));
    fd_table_destroy(&child);
    assert(fd_close(a,pfd[0])==0 && fd_close(a,pfd[1])==0 && fd_close(a,dev)==0 && !fd_pipe_count());
    (void)b;
}
int main(void) {
    assert(vfs_mkdir("/tmp",0700)==-ERROR_EXISTS);
    assert(vfs_mkdir("/tmp/tree",0700)==0);
    assert(vfs_mkdir("/tmp/tree/child",0700)==0);
    assert(vfs_rmdir("/tmp/tree")==-ERROR_NOT_EMPTY);
    struct fd_table a,b;assert(fd_table_init(&a)&&fd_table_init(&b));
    int dir_fd=fd_open(&a,"/tmp/tree",NOTOS_O_RDONLY,0);
    assert(dir_fd>=0 && vfs_counts().nodes==2 && vfs_counts().references==1);
    struct notos_directory_entry dirent;struct notos_file_info dir_info;
    assert(fd_readdir(&a,dir_fd,&dirent)==1 && !strcmp(dirent.name,"child"));
    assert(vfs_rmdir("/tmp/tree/child")==0 && vfs_rmdir("/tmp/tree")==0);
    assert(vfs_counts().nodes==1 && vfs_counts().references==1);
    assert(fd_stat(&a,dir_fd,&dir_info)==0 && dir_info.links==0);
    assert(fd_readdir(&a,dir_fd,&dirent)==-ERROR_NOT_FOUND);
    assert(fd_close(&a,dir_fd)==0 && vfs_counts().nodes==0);
    int root1=fd_open(&a,"/tmp",NOTOS_O_RDONLY,0);
    int root2=fd_open(&b,"/tmp",NOTOS_O_RDONLY,0);
    assert(root1>=0 && root2>=0 && vfs_counts().nodes==1 && vfs_counts().references==2);
    assert(fd_close(&a,root1)==0 && fd_close(&b,root2)==0 && vfs_counts().nodes==0);
    assert(vfs_unlink("/tmp")==-ERROR_IS_DIRECTORY);
    struct notos_pollfd watched[]={{0,NOTOS_POLLIN,99},{1,NOTOS_POLLOUT,99},{-1,0xffff,99},{15,0,99},{1,NOTOS_POLLIN,99}};
    assert(fd_poll(&a,watched,5)==2 && !watched[0].revents && watched[1].revents==NOTOS_POLLOUT
        && !watched[2].revents && watched[3].revents==NOTOS_POLLNVAL && !watched[4].revents);
    assert(fd_feed(&a,"x",1,0)==0 && fd_poll(&a,watched,1)==1 && watched[0].revents==NOTOS_POLLIN);
    char queued;assert(fd_read(&a,0,&queued,1)==1 && queued=='x' && fd_poll(&a,watched,1)==0);
    assert(fd_feed(&a,0,0,1)==0 && fd_poll(&a,watched,1)==1 && watched[0].revents==(NOTOS_POLLIN|NOTOS_POLLHUP));
    fd_table_destroy(&a);assert(fd_table_init(&a));
    int f=fd_open(&a,"/tmp/test",NOTOS_O_CREAT|NOTOS_O_RDWR,0600);assert(f==3);
    assert(fd_open(&b,"/tmp/test",NOTOS_O_CREAT|NOTOS_O_EXCL|NOTOS_O_RDWR,0600)==-ERROR_EXISTS);
    assert(fd_write(&a,f,"abcdef",6)==6 && fd_seek(&a,f,0,NOTOS_SEEK_SET)==0);
    struct notos_pollfd file_event={f,NOTOS_POLLIN|NOTOS_POLLOUT,0};
    assert(fd_poll(&a,&file_event,1)==1 && file_event.revents==(NOTOS_POLLIN|NOTOS_POLLOUT));
    int d=fd_dup(&a,f,-1);char text[16]={0};assert(d==4);
    assert(fd_read(&a,d,text,2)==2&&!memcmp(text,"ab",2));assert(fd_read(&a,f,text,2)==2&&!memcmp(text,"cd",2));
    int separate=fd_open(&b,"/tmp/test",NOTOS_O_RDONLY,0);assert(separate==3);
    assert(fd_read(&b,separate,text,2)==2&&!memcmp(text,"ab",2));
    assert(fd_control(&a,d,NOTOS_F_SETFD,NOTOS_FD_CLOEXEC)==0 && fd_control(&a,f,NOTOS_F_GETFD,0)==0);
    assert(fd_control(&a,0,NOTOS_F_GETTTY,0)==0 && fd_control(&a,0,NOTOS_F_SETTTY,NOTOS_TTY_RAW)==0);
    assert(fd_control(&a,1,NOTOS_F_GETTTY,0)==(int)NOTOS_TTY_RAW);
    assert(fd_control(&a,0,NOTOS_F_SETTTY,2)==-ERROR_INVALID_ARGUMENT);
    assert(fd_control(&a,f,NOTOS_F_GETTTY,0)==-ERROR_INVALID_ARGUMENT);
    assert(fd_control(&a,f,NOTOS_F_SETTTY,0)==-ERROR_INVALID_ARGUMENT);
    assert(fd_control(&a,0,NOTOS_F_SETTTY,0)==0 && fd_control(&a,0,NOTOS_F_GETTTY,0)==0);
    assert(fd_feed(&a,"hi\r\x04next\n",9,0)==0);
    assert(fd_forward_tty(&a,&b,16)==3 && fd_read(&b,0,text,16)==3 && !memcmp(text,"hi\n",3));
    assert(fd_forward_tty(&a,&b,16)==0 && b.input_eof==1 && fd_read(&b,0,text,16)==0);
    b.input_eof=0;
    assert(fd_read(&a,0,text,16)==5 && !memcmp(text,"next\n",5));
    assert(fd_control(&a,d,NOTOS_F_SETFL,NOTOS_O_APPEND)==0);
    assert(fd_seek(&a,f,0,NOTOS_SEEK_SET)==0 && fd_write(&a,f,"!",1)==1);
    struct notos_file_info info;assert(fd_stat(&a,f,&info)==0&&info.size==7&&info.links==1&&info.mtime==1735689600ULL);
    assert(vfs_rename("/tmp/test","/disk/x")==-ERROR_CROSS_DEVICE);
    assert(vfs_rename("/disk/sample","/tmp/x")==-ERROR_CROSS_DEVICE);
    int victim=fd_open(&b,"/tmp/victim",NOTOS_O_CREAT|NOTOS_O_RDWR,0600);
    assert(victim>=0 && fd_write(&b,victim,"old",3)==3);
    assert(vfs_rename("/tmp/test","/tmp/victim")==0);
    assert(vfs_stat("/tmp/test",&info)==-ERROR_NOT_FOUND);
    assert(fd_stat(&b,victim,&info)==0 && info.links==0 && info.size==3);
    assert(fd_close(&b,victim)==0);
    assert(vfs_rename("/tmp/victim","/tmp/test")==0);
    assert(vfs_unlink("/tmp/test")==0 && vfs_stat("/tmp/test",&info)==-ERROR_NOT_FOUND);
    assert(fd_stat(&a,f,&info)==0&&info.links==0);
    assert(fd_close(&a,f)==0&&fd_close(&a,f)==-ERROR_BAD_FD);
    fd_close(&a,d);fd_close(&b,separate);assert(vfs_counts().nodes==0);
    f=fd_open(&a,"/tmp/hole",NOTOS_O_CREAT|NOTOS_O_RDWR,0600);
    assert(fd_seek(&a,f,8191,NOTOS_SEEK_SET)==8191&&fd_write(&a,f,"x",1)==1);
    assert(fd_seek(&a,f,8180,NOTOS_SEEK_SET)==8180&&fd_read(&a,f,text,12)==12);
    for (unsigned i=0;i<11;i++) assert(text[i]==0);
    assert(text[11]=='x');
    assert(fd_truncate(&a,f,1)==0&&vfs_counts().pages==1);
    assert(fd_truncate(&a,f,8192)==0);assert(fd_seek(&a,f,8180,NOTOS_SEEK_SET)==8180&&fd_read(&a,f,text,12)==12);
    for (unsigned i=0;i<12;i++) assert(!text[i]);
    assert(fd_truncate(&a,f,0)==0);fail_after=1;
    assert(fd_truncate(&a,f,16384)==-ERROR_MEMORY && !vfs_counts().pages);
    assert(fd_stat(&a,f,&info)==0&&!info.size);fail_after=-1;
    assert(fd_seek(&a,f,65536,NOTOS_SEEK_SET)==65536&&fd_write(&a,f,"x",1)==-ERROR_FILE_TOO_LARGE);
    vfs_unlink("/tmp/hole");fd_close(&a,f);
    char name[32];int files[5];
    for (int i=0;i<5;i++) {snprintf(name,sizeof(name),"/tmp/quota%d",i);files[i]=fd_open(&a,name,NOTOS_O_CREAT|NOTOS_O_RDWR,0600);assert(files[i]>=0);}
    for (int i=0;i<4;i++) assert(fd_truncate(&a,files[i],65536)==0);
    assert(fd_truncate(&a,files[4],1)==-ERROR_NO_SPACE && vfs_counts().pages==64);
    for (int i=0;i<5;i++) {snprintf(name,sizeof(name),"/tmp/quota%d",i);vfs_unlink(name);fd_close(&a,files[i]);}
    f=fd_open(&a,"/disk/sample",NOTOS_O_RDONLY,0);assert(f>=0&&fd_read(&a,f,text,6)==6&&!memcmp(text,"disk\0!",6));
    struct notos_file_info disk;assert(fd_path_stat("/disk/sample",&disk)==0&&fd_stat(&a,f,&info)==0&&info.inode==disk.inode);
    int writable=fd_open(&a,"/disk/sample",NOTOS_O_WRONLY,0);assert(writable>=0);fd_close(&a,writable);fd_close(&a,f);
    assert(fd_read(&a,0,text,1)==-ERROR_AGAIN);
    assert(fd_feed(&a,"a\n",2,1)==0&&fd_read(&a,0,text,16)==2&&text[0]=='a'&&fd_read(&a,0,text,1)==0);
    assert(fd_read(&b,0,text,1)==-ERROR_AGAIN);
    assert(fd_seek(&a,0,0,NOTOS_SEEK_SET)==-ERROR_ILLEGAL_SEEK);
    assert(fd_open(&a,"/tmp/../x",NOTOS_O_RDONLY,0)==-ERROR_INVALID_ARGUMENT);
    assert(fd_open(&a,"/tmp/.",NOTOS_O_CREAT|NOTOS_O_RDWR,0)==-ERROR_INVALID_ARGUMENT);
    assert(fd_open(&a,"/tmp/missing",NOTOS_O_RDONLY,0)==-ERROR_NOT_FOUND);
    int full[13];
    for (unsigned i=0;i<13;i++) {full[i]=fd_open(&a,"/dev/null",NOTOS_O_RDWR,0);assert(full[i]==(int)i+3);}
    assert(fd_open(&a,"/tmp/no-side-effect",NOTOS_O_CREAT|NOTOS_O_RDWR,0600)==-ERROR_TOO_MANY_FILES);
    assert(vfs_stat("/tmp/no-side-effect",&info)==-ERROR_NOT_FOUND);
    for (unsigned i=0;i<13;i++) assert(fd_close(&a,full[i])==0);
    /* Exercise 16-bit inode wrap while an unlinked old inode remains open. */
    int pinned=fd_open(&a,"/tmp/pinned",NOTOS_O_CREAT|NOTOS_O_RDWR,0600);
    struct notos_file_info pin;assert(pinned>=0 && fd_stat(&a,pinned,&pin)==0 && vfs_unlink("/tmp/pinned")==0);
    for (unsigned i=0;i<70000;i++) {
        int transient=fd_open(&a,"/tmp/transient",NOTOS_O_CREAT|NOTOS_O_RDWR,0600);assert(transient>=0);
        assert(fd_stat(&a,transient,&info)==0 && info.inode>=16 && info.inode<=65535 && info.inode!=pin.inode);
        assert(vfs_unlink("/tmp/transient")==0 && fd_close(&a,transient)==0);
    }
    fd_close(&a,pinned);
    test_pipes_and_spawn_fds(&a,&b);
    fd_table_destroy(&a);fd_table_destroy(&b);empty();
    puts("PASS: tmpfs holes/truncate/unlink/rename/quotas/OOM rollback, NotFS snapshots, private FDs/shared dup offsets, pipes, spawn FDs and stdin queues");
}

/* This native unit isolates descriptor/VFS logic, not network correctness. */
int socket_create(void) {return -ERROR_UNSUPPORTED;}
void socket_release(int h) {(void)h;}
int socket_read(int h,void *p,u32 n,u32 f) {(void)h;(void)p;(void)n;(void)f;return -ERROR_UNSUPPORTED;}
int socket_write(int h,const void *p,u32 n) {(void)h;(void)p;(void)n;return -ERROR_UNSUPPORTED;}
void socket_resolve_cancel(struct fd_table *t) {(void)t;}

u16 socket_ready(int handle) {(void)handle;return NOTOS_POLLNVAL;}

const u8 *system_file_lookup(const char *path,u32 *bytes) {(void)path;(void)bytes;return 0;}

int fs_info(const char *name, struct notos_file_info *info) {
    if (strcmp(name,"sample")) return -ERROR_NOT_FOUND;
    *info=(struct notos_file_info){256,6,NOTOS_FILE_REGULAR,1,0644,0,0,1735689600ULL};return 0;
}
int fs_open_file(const char *name,u32 flags,u32 mode) {(void)flags;(void)mode;return strcmp(name,"sample")?-ERROR_NOT_FOUND:0;}
int fs_read_range(const char *name,u32 offset,void *buffer,u32 bytes) {
    assert(!strcmp(name,"sample"));if(offset>=6)return 0;if(bytes>6-offset)bytes=6-offset;
    memcpy(buffer,"disk\0!"+offset,bytes);return (int)bytes;
}
int fs_write_range(const char *name,u32 inode,u32 offset,const void *buffer,u32 bytes) {(void)name;(void)inode;(void)offset;(void)buffer;(void)bytes;return -ERROR_UNSUPPORTED;}
int fs_truncate_file(const char *name,u32 inode,u32 bytes) {(void)name;(void)inode;(void)bytes;return -ERROR_UNSUPPORTED;}
int fs_remove(const char *name,int directory) {(void)name;(void)directory;return -ERROR_UNSUPPORTED;}
int fs_rename(const char *old_name,const char *new_name) {(void)old_name;(void)new_name;return -ERROR_UNSUPPORTED;}
int fs_mkdir(const char *name,u32 mode) {(void)name;(void)mode;return -ERROR_UNSUPPORTED;}
int fs_entry(const char *name,u32 index,char child[24],struct notos_file_info *info) {(void)name;(void)index;(void)child;(void)info;return 0;}
