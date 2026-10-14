#include <assert.h>
#include <stdio.h>
#include <string.h>
#include "../kernel/fs.h"
#include "../kernel/fs_core.h"
#include "../kernel/syscall_numbers.h"
static unsigned char disk[128*1024], saved[128*1024];
static int fail_after=-1;
int disk_init(void){return 1;}
u32 disk_sectors(void){return 256;}
int disk_read(u32 sector,void *buffer){assert(sector<256);memcpy(buffer,disk+sector*512,512);return 1;}
int disk_write(u32 sector,const void *buffer){assert(sector<256);if(!fail_after)return 0;if(fail_after>0)fail_after--;memcpy(disk+sector*512,buffer,512);return 1;}
int disk_flush(void){if(!fail_after)return 0;if(fail_after>0)fail_after--;return 1;}
void console_puts(const char *text){(void)text;}void console_putc(char c){(void)c;}void console_uint(u32 n){(void)n;}
static void legacy(void){
    memset(disk,0,sizeof(disk));u32 header[]={FS_CORE_MAGIC,1,5,0};memcpy(disk,header,16);
    memcpy(disk+512,"legacy",7);u32 size=6;memcpy(disk+536,&size,4);memcpy(disk+540,"old-v1",6);
    u32 checksum=fs_core_crc(disk,33*512);memcpy(disk+12,&checksum,4);
}
int main(void){
    fs_init();assert(!fs_ready());fs_command("mkfs","");assert(fs_ready());
    assert(!fs_mkdir("dir",0755));assert(!fs_open_file("dir/file",NOTOS_O_CREAT|NOTOS_O_RDWR,0600));
    u32 inode=(u32)fs_file_inode("dir/file");assert(inode>=256);
    assert(fs_write_range("dir/file",inode,4097,"x",1)==1);unsigned char buffer[5000];
    assert(fs_read_range("dir/file",0,buffer,sizeof(buffer))==4098);for(unsigned i=0;i<4097;i++)assert(!buffer[i]);assert(buffer[4097]=='x');
    assert(fs_remove("dir",1)==-ERROR_NOT_EMPTY);assert(fs_truncate_file("dir",0,0)==-ERROR_IS_DIRECTORY);
    memcpy(saved,disk,sizeof(disk));
    for(int failure=0;failure<133;failure++){
        memcpy(disk,saved,sizeof(disk));fail_after=-1;fs_init();fail_after=failure;
        int result=fs_write_range("dir/file",inode,0,"replacement",11);
        fail_after=-1;fs_init();assert(fs_ready());int size=fs_read_range("dir/file",0,buffer,sizeof(buffer));assert(size==4098);
        assert(buffer[0]==0 || !memcmp(buffer,"replacement",11));assert(result==11 || result==-ERROR_IO);
    }
    fail_after=-1;memcpy(disk,saved,sizeof(disk));fs_init();
    struct fs_core_snapshot malformed;memcpy(&malformed,disk,sizeof(malformed));
    memset(malformed.entries[1].path,'x',sizeof(malformed.entries[1].path));
    malformed.checksum=fs_core_crc(&malformed,sizeof(malformed));assert(!fs_core_valid(&malformed));
    assert(!fs_truncate_file("dir/file",inode,FS_FILE_MAX));
    assert(!fs_open_file("other",NOTOS_O_CREAT|NOTOS_O_RDWR,0644));assert(fs_write_range("other",(u32)fs_file_inode("other"),0,"x",1)==-ERROR_NO_SPACE);
    assert(!fs_remove("dir/file",0));assert(!fs_remove("dir",1));
    memcpy(&malformed,disk,sizeof(malformed));
    for(unsigned i=0;i<FS_CORE_NODES;i++) if(!malformed.entries[i].kind) assert(!malformed.entries[i].offset);
    legacy();memcpy(saved,disk,sizeof(disk));
    for(int failure=0;failure<133;failure++){
        memcpy(disk,saved,sizeof(disk));fail_after=failure;fs_init();assert(!memcmp(disk,saved,66*512));
        fail_after=-1;fs_init();assert(fs_ready());assert(fs_read_range("legacy",0,buffer,6)==6&&!memcmp(buffer,"old-v1",6));
    }
    puts("PASS: NotFS v2 holes/directories/quota, all publication failure points and transactional v1 migration");
}
