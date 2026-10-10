/* Host-only fake frame allocator. Low static addresses, never a guest libc dependency. */
#include <assert.h>
#include <stdio.h>
#include <string.h>
#include "../kernel/fd.h"
#include "../kernel/vfs.h"
#include "../kernel/syscall_numbers.h"
static unsigned char pages[64][4096] __attribute__((aligned(4096)));
static int owned[64],fail_after=-1;
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
int fs_file_size(const char *name) {return !strcmp(name,"sample")?6:-1;}
int fs_read_file(const char *name,void *buffer,u32 size) {assert(!strcmp(name,"sample")&&size>=6);memcpy(buffer,"disk\0!",6);return 6;}
void console_putc(char c) {(void)c;}
void console_puts(const char *s) {(void)s;}
void console_uint(u32 n) {(void)n;}
static void empty(void) {struct vfs_counts c=vfs_counts();assert(!c.nodes&&!c.pages&&!c.files&&!c.references&&!fd_description_count());}
int main(void) {
    struct fd_table a,b;assert(fd_table_init(&a)&&fd_table_init(&b));
    int f=fd_open(&a,"/tmp/test",NOTOS_O_CREAT|NOTOS_O_RDWR,0600);assert(f==3);
    assert(fd_open(&b,"/tmp/test",NOTOS_O_CREAT|NOTOS_O_EXCL|NOTOS_O_RDWR,0600)==-ERROR_EXISTS);
    assert(fd_write(&a,f,"abcdef",6)==6 && fd_seek(&a,f,0,NOTOS_SEEK_SET)==0);
    int d=fd_dup(&a,f,-1);char text[16]={0};assert(d==4);
    assert(fd_read(&a,d,text,2)==2&&!memcmp(text,"ab",2));assert(fd_read(&a,f,text,2)==2&&!memcmp(text,"cd",2));
    int separate=fd_open(&b,"/tmp/test",NOTOS_O_RDONLY,0);assert(separate==3);
    assert(fd_read(&b,separate,text,2)==2&&!memcmp(text,"ab",2));
    assert(fd_control(&a,d,NOTOS_F_SETFD,NOTOS_FD_CLOEXEC)==0 && fd_control(&a,f,NOTOS_F_GETFD,0)==0);
    assert(fd_control(&a,d,NOTOS_F_SETFL,NOTOS_O_APPEND)==0);
    assert(fd_seek(&a,f,0,NOTOS_SEEK_SET)==0 && fd_write(&a,f,"!",1)==1);
    struct notos_file_info info;assert(fd_stat(&a,f,&info)==0&&info.size==7&&info.links==1);
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
    assert(fd_open(&a,"/disk/sample",NOTOS_O_WRONLY,0)==-ERROR_READ_ONLY);fd_close(&a,f);
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
    fd_table_destroy(&a);fd_table_destroy(&b);empty();
    puts("PASS: tmpfs holes/truncate/unlink/quotas/OOM rollback, NotFS snapshots, private FDs/shared dup offsets and stdin queues");
}
