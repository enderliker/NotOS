#include <assert.h>
#include <stdio.h>
#include <string.h>
#include "../kernel/fs.h"
#include "../kernel/fs_core.h"
#include "../kernel/platform_tls.h"
#include "../kernel/syscall_numbers.h"
static unsigned char disk[256*1024], saved[256*1024];
static int fail_after=-1;
static u64 fake_epoch=1700000100ULL;
int rtc_utc(struct utc_time *out){*out=(struct utc_time){719528u+(u32)(fake_epoch/86400ULL),(u32)(fake_epoch%86400ULL)};return 1;}
int disk_init(void){return 1;}
u32 disk_sectors(void){return 512;}
int disk_read(u32 sector,void *buffer){assert(sector<512);memcpy(buffer,disk+sector*512,512);return 1;}
int disk_write(u32 sector,const void *buffer){assert(sector<512);if(!fail_after)return 0;if(fail_after>0)fail_after--;memcpy(disk+sector*512,buffer,512);return 1;}
int disk_flush(void){if(!fail_after)return 0;if(fail_after>0)fail_after--;return 1;}
void console_puts(const char *text){(void)text;}void console_putc(char c){(void)c;}void console_uint(u32 n){(void)n;}
static void legacy_v1(void){
    memset(disk,0,sizeof(disk));u32 header[]={FS_CORE_MAGIC,1,5,0};memcpy(disk,header,16);
    memcpy(disk+512,"legacy",7);u32 size=6;memcpy(disk+536,&size,4);memcpy(disk+540,"old-v1",6);
    u32 checksum=fs_core_crc(disk,33*512);memcpy(disk+12,&checksum,4);
}
static void legacy_v2(void){
    memset(disk,0,sizeof(disk));
    static struct fs_v2_snapshot v2;
    memset(&v2,0,sizeof(v2));
    v2.magic=FS_CORE_MAGIC;v2.version=2;v2.generation=9;v2.next_inode=258;
    memcpy(v2.entries[0].path,"v2dir",6);
    v2.entries[0].inode=256;v2.entries[0].kind=NOTOS_FILE_DIRECTORY;v2.entries[0].permissions=0755;
    memcpy(v2.entries[1].path,"v2dir/note",11);
    v2.entries[1].inode=257;v2.entries[1].kind=NOTOS_FILE_REGULAR;v2.entries[1].permissions=0640;
    v2.entries[1].size=9;v2.entries[1].offset=0;
    memcpy(v2.data,"from-v2!!",9);
    v2.checksum=fs_core_crc(&v2,sizeof(v2));
    assert(fs_v2_valid(&v2));
    memcpy(disk,&v2,sizeof(v2));
}
int main(void){
    fs_init();assert(!fs_ready());fs_command("mkfs","");assert(fs_ready());
    fake_epoch=1700000200ULL;
    assert(!fs_mkdir("dir",0755));
    fake_epoch=1700000300ULL;
    assert(!fs_open_file("dir/file",NOTOS_O_CREAT|NOTOS_O_RDWR,0600));
    u32 inode=(u32)fs_file_inode("dir/file");assert(inode>=256);
    struct notos_file_info info;
    assert(!fs_info("dir/file",&info) && info.mtime==1700000300ULL && info.permissions==0600 && info.uid==0 && info.gid==0);
    fake_epoch=1700000400ULL;
    assert(fs_write_range("dir/file",inode,4097,"x",1)==1);unsigned char buffer[5000];
    assert(!fs_info("dir/file",&info) && info.mtime==1700000400ULL && info.size==4098);
    assert(fs_read_range("dir/file",0,buffer,sizeof(buffer))==4098);for(unsigned i=0;i<4097;i++)assert(!buffer[i]);assert(buffer[4097]=='x');
    assert(fs_remove("dir",1)==-ERROR_NOT_EMPTY);assert(fs_truncate_file("dir",0,0)==-ERROR_IS_DIRECTORY);
    memcpy(saved,disk,sizeof(disk));
    for(int failure=0;failure<196;failure++){
        memcpy(disk,saved,sizeof(disk));fail_after=-1;fs_init();fail_after=failure;
        int result=fs_write_range("dir/file",inode,0,"replacement",11);
        fail_after=-1;fs_init();assert(fs_ready());int size=fs_read_range("dir/file",0,buffer,sizeof(buffer));assert(size==4098);
        assert(buffer[0]==0 || !memcmp(buffer,"replacement",11));assert(result==11 || result==-ERROR_IO);
    }
    fail_after=-1;memcpy(disk,saved,sizeof(disk));fs_init();
    /* Test atomic rename including subtree rename, overwrite, error cases, and mid-commit fault injection. */
    assert(!fs_open_file("target",NOTOS_O_CREAT|NOTOS_O_RDWR,0644));
    u32 target_inode=(u32)fs_file_inode("target");
    assert(fs_write_range("target",target_inode,0,"old",3)==3);
    assert(fs_rename("dir","dir/sub")==-ERROR_INVALID_ARGUMENT);
    assert(fs_rename("dir","target")==-ERROR_NOT_DIRECTORY);
    assert(fs_rename("target","dir")==-ERROR_IS_DIRECTORY);
    memcpy(saved,disk,sizeof(disk));
    for(int failure=0;failure<196;failure++){
        memcpy(disk,saved,sizeof(disk));fail_after=-1;fs_init();fail_after=failure;
        int result=fs_rename("dir/file","target");
        fail_after=-1;fs_init();assert(fs_ready());
        int old_exists=(fs_info("dir/file",&info)==0);
        struct notos_file_info tinfo;
        assert(!fs_info("target",&tinfo));
        if(old_exists){
            assert(result==-ERROR_IO && tinfo.inode==target_inode && tinfo.size==3);
        }else{
            assert((result==0 || result==-ERROR_IO) && tinfo.inode==inode && tinfo.size==4098);
        }
        if(result==0) assert(!old_exists && tinfo.inode==inode && tinfo.size==4098);
    }
    fail_after=-1;memcpy(disk,saved,sizeof(disk));fs_init();
    assert(!fs_rename("dir","moved_dir"));
    assert(fs_info("dir/file",&info)==-ERROR_NOT_FOUND);
    assert(!fs_info("moved_dir/file",&info) && info.inode==inode && info.size==4098);
    assert(!fs_rename("moved_dir","dir"));
    assert(!fs_remove("target",0));
    /* Test bounded persistent inode recycling across wraparound [256, 65535]. */
    static struct fs_core_snapshot snap;
    u32 active_sector = 0;
    memcpy(&snap,disk,sizeof(snap));
    struct fs_core_snapshot snap1;
    memcpy(&snap1,disk+256*512,sizeof(snap1));
    if(fs_core_valid(&snap1) && (!fs_core_valid(&snap) || snap1.generation>snap.generation)){
        snap=snap1;active_sector=256;
    }
    snap.next_inode=65535;
    snap.checksum=fs_core_crc(&snap,sizeof(snap));
    memcpy(disk+active_sector*512,&snap,sizeof(snap));
    fs_init();assert(fs_ready());
    assert(!fs_open_file("wrap1",NOTOS_O_CREAT|NOTOS_O_RDWR,0644));
    assert(fs_file_inode("wrap1")==65535);
    assert(!fs_open_file("wrap2",NOTOS_O_CREAT|NOTOS_O_RDWR,0644));
    /* Inodes 256 (dir) and 257 (dir/file) are live, so wraparound must skip them and pick 258. */
    assert(fs_file_inode("wrap2")==258);
    assert(!fs_remove("wrap1",0) && !fs_remove("wrap2",0));
    struct fs_core_snapshot malformed=snap;
    memset(malformed.entries[1].path,'x',sizeof(malformed.entries[1].path));
    malformed.checksum=fs_core_crc(&malformed,sizeof(malformed));assert(!fs_core_valid(&malformed));
    assert(!fs_truncate_file("dir/file",inode,FS_FILE_MAX));
    assert(!fs_open_file("other",NOTOS_O_CREAT|NOTOS_O_RDWR,0644));assert(fs_write_range("other",(u32)fs_file_inode("other"),0,"x",1)==-ERROR_NO_SPACE);
    assert(!fs_remove("dir/file",0));assert(!fs_remove("dir",1));assert(!fs_remove("other",0));
    /* Test v2 -> v3 transactional migration under fault injection. */
    legacy_v2();memcpy(saved,disk,sizeof(disk));
    for(int failure=0;failure<196;failure++){
        memcpy(disk,saved,sizeof(disk));fail_after=failure;fs_init();assert(!memcmp(disk,saved,128*512));
        fail_after=-1;fs_init();assert(fs_ready());
        assert(!fs_info("v2dir/note",&info) && info.inode==257 && info.permissions==0640 && info.size==9);
        assert(fs_read_range("v2dir/note",0,buffer,9)==9 && !memcmp(buffer,"from-v2!!",9));
    }
    /* Test v1 -> v3 transactional migration under fault injection. */
    legacy_v1();memcpy(saved,disk,sizeof(disk));
    for(int failure=0;failure<196;failure++){
        memcpy(disk,saved,sizeof(disk));fail_after=failure;fs_init();assert(!memcmp(disk,saved,66*512));
        fail_after=-1;fs_init();assert(fs_ready());assert(fs_read_range("legacy",0,buffer,6)==6&&!memcmp(buffer,"old-v1",6));
    }
    puts("PASS: NotFS v3 holes/directories/quota/mtime/rename/inode-recycling, all publication failure points and transactional v1/v2->v3 migration");
}
