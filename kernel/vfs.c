/* Bounded tmpfs plus read-only NotFS snapshots; no user pointers reach this layer. */
#include "vfs.h"
#include "physical.h"
#include "fs.h"
#include "syscall_numbers.h"
#define NODE_NAME_BYTES 24u
#define NODE_PAGES (VFS_FILE_BYTES/4096u)
struct vfs_node {
    u32 used,disk,linked,references,inode,size,permissions;
    char name[NODE_NAME_BYTES];
    u32 pages[NODE_PAGES];
};
static struct vfs_node nodes[VFS_NODE_COUNT];
static u32 page_count,next_inode=16;
static void copy(void *destination,const void *source,u32 bytes) {
    u8 *d=destination;const u8 *s=source;
    while (bytes--) *d++=*s++;
}
static u8 *page_bytes(u32 frame) {return (u8 *)(unsigned long)frame;}
static int path_name(const char *path,int *disk,const char **name) {
    if (starts(path,"/tmp/")) {*disk=0;*name=path+5;}
    else if (starts(path,"/disk/")) {*disk=1;*name=path+6;}
    else return -ERROR_NOT_FOUND;
    unsigned bytes=0;
    for (const char *s=*name;*s;s++) {
        char c=*s;
        if (++bytes>=NODE_NAME_BYTES) return -ERROR_NAME_TOO_LONG;
        if (!((c>='a'&&c<='z')||(c>='A'&&c<='Z')||(c>='0'&&c<='9')||c=='.'||c=='_'||c=='-'))
            return -ERROR_INVALID_ARGUMENT;
    }
    if (!bytes || equal(*name,".") || equal(*name,"..")) return -ERROR_INVALID_ARGUMENT;
    return 0;
}
static struct vfs_node *find(const char *name) {
    for (u32 i=0;i<VFS_NODE_COUNT;i++)
        if (nodes[i].used && !nodes[i].disk && nodes[i].linked && equal(nodes[i].name,name)) return &nodes[i];
    return 0;
}
static void dispose(struct vfs_node *node) {
    for (u32 i=0;i<NODE_PAGES;i++) if (node->pages[i]) {
        physical_free(node->pages[i]);page_count--;
    }
    *node=(struct vfs_node){0};
}
void vfs_release(struct vfs_node *node) {
    if (!node || !node->used || !node->references) return;
    node->references--;
    if (!node->references && (!node->linked || node->disk)) dispose(node);
}
/* Reserve all growth before changing contents/size; quota or physical OOM rolls back. */
static int reserve(struct vfs_node *node,u32 bytes) {
    u32 count=(bytes+4095)/4096,added[NODE_PAGES]={0};
    for (u32 i=0;i<count;i++) if (!node->pages[i]) {
        if (page_count==VFS_PAGE_COUNT) goto no_space;
        u32 frame=physical_alloc();
        if (!frame) goto no_memory;
        node->pages[i]=added[i]=frame;page_count++;
        for (u32 j=0;j<4096;j++) page_bytes(frame)[j]=0;
    }
    return 0;
no_space:
    for (u32 i=0;i<count;i++) if (added[i]) {physical_free(added[i]);node->pages[i]=0;page_count--;}
    return -ERROR_NO_SPACE;
no_memory:
    for (u32 i=0;i<count;i++) if (added[i]) {physical_free(added[i]);node->pages[i]=0;page_count--;}
    return -ERROR_MEMORY;
}
static u32 disk_inode(const char *name) {
    return 7u+(u32)fs_file_inode(name);
}
/* Picolibc's i386 ino_t is 16-bit. Keep IDs distinct across live nodes and devices. */
static u32 allocate_inode(void) {
    for (u32 attempt=0;attempt<=VFS_NODE_COUNT;attempt++) {
        if (next_inode>65535u) next_inode=16;
        u32 candidate=next_inode++;int busy=0;
        for (u32 i=0;i<VFS_NODE_COUNT;i++) if (nodes[i].used && nodes[i].inode==candidate) busy=1;
        if (!busy) return candidate;
    }
    return 0;
}
void vfs_node_info(const struct vfs_node *node,struct notos_file_info *result) {
    *result=(struct notos_file_info){node->inode,node->size,NOTOS_FILE_REGULAR,node->linked,node->permissions};
}
int vfs_open(const char *path,u32 flags,u32 mode,struct vfs_node **result) {
    int disk;const char *name;int error=path_name(path,&disk,&name);
    if (error) return error;
    struct vfs_node *node=0;
    if (disk) {
        if ((flags&NOTOS_O_ACCMODE)!=NOTOS_O_RDONLY || (flags&(NOTOS_O_CREAT|NOTOS_O_TRUNC))) return -ERROR_READ_ONLY;
        if (!fs_ready()) return -ERROR_IO;
        if (fs_file_size(name)<0) return -ERROR_NOT_FOUND;
    } else {
        node=find(name);
        if (node && (flags&(NOTOS_O_CREAT|NOTOS_O_EXCL))==(NOTOS_O_CREAT|NOTOS_O_EXCL)) return -ERROR_EXISTS;
        if (!node && !(flags&NOTOS_O_CREAT)) return -ERROR_NOT_FOUND;
    }
    int created=!node;
    if (created) {
        struct vfs_counts counts=vfs_counts();
        if (!disk && counts.files==VFS_FILE_COUNT) return -ERROR_NO_SPACE;
        for (u32 i=0;i<VFS_NODE_COUNT;i++) if (!nodes[i].used) {node=&nodes[i];break;}
        if (!node) return -ERROR_NO_SPACE;
        u32 inode=disk?disk_inode(name):allocate_inode();
        if (!inode) return -ERROR_NO_SPACE;
        *node=(struct vfs_node){.used=1,.disk=(u32)disk,.linked=1,.inode=inode,
            .permissions=disk?0444u:mode&0777u};
        copy(node->name,name,length(name)+1);
        if (disk) {
            int size=fs_file_size(name);
            error=reserve(node,(u32)size);
            if (!error && size && fs_read_file(name,page_bytes(node->pages[0]),4096)!=size) error=-ERROR_IO;
            if (error) {dispose(node);return error;}
            node->size=(u32)size;
        }
    }
    if (flags&NOTOS_O_TRUNC) {
        error=vfs_truncate(node,0);
        if (error) {if (created) dispose(node);return error;}
    }
    node->references++;*result=node;
    return 0;
}
int vfs_read(struct vfs_node *node,u32 offset,void *buffer,u32 bytes) {
    if (offset>=node->size) return 0;
    if (bytes>node->size-offset) bytes=node->size-offset;
    u32 done=0;
    while (done<bytes) {
        u32 at=offset+done,chunk=4096-(at&4095);
        if (chunk>bytes-done) chunk=bytes-done;
        copy((u8 *)buffer+done,page_bytes(node->pages[at/4096])+(at&4095),chunk);
        done+=chunk;
    }
    return (int)bytes;
}
int vfs_write(struct vfs_node *node,u32 offset,const void *buffer,u32 bytes) {
    if (node->disk) return -ERROR_READ_ONLY;
    if (!bytes) return 0;
    if (offset>VFS_FILE_BYTES || bytes>VFS_FILE_BYTES-offset) return -ERROR_FILE_TOO_LARGE;
    int error=reserve(node,offset+bytes);if (error) return error;
    u32 done=0;
    while (done<bytes) {
        u32 at=offset+done,chunk=4096-(at&4095);
        if (chunk>bytes-done) chunk=bytes-done;
        copy(page_bytes(node->pages[at/4096])+(at&4095),(const u8 *)buffer+done,chunk);
        done+=chunk;
    }
    if (offset+bytes>node->size) node->size=offset+bytes;
    return (int)bytes;
}
int vfs_truncate(struct vfs_node *node,u32 bytes) {
    if (node->disk) return -ERROR_READ_ONLY;
    if (bytes>VFS_FILE_BYTES) return -ERROR_FILE_TOO_LARGE;
    if (bytes>node->size) {int error=reserve(node,bytes);if (error) return error;}
    if (bytes<node->size) {
        u32 keep=(bytes+4095)/4096;
        for (u32 i=keep;i<NODE_PAGES;i++) if (node->pages[i]) {
            physical_free(node->pages[i]);node->pages[i]=0;page_count--;
        }
        if (bytes&4095) for (u32 i=bytes&4095;i<4096;i++) page_bytes(node->pages[bytes/4096])[i]=0;
    }
    node->size=bytes;return 0;
}
int vfs_unlink(const char *path) {
    int disk;const char *name;int error=path_name(path,&disk,&name);
    if (error) return error;
    if (disk) return -ERROR_READ_ONLY;
    struct vfs_node *node=find(name);if (!node) return -ERROR_NOT_FOUND;
    node->linked=0;if (!node->references) dispose(node);
    return 0;
}
int vfs_stat(const char *path,struct notos_file_info *result) {
    int disk;const char *name;int error=path_name(path,&disk,&name);
    if (error) return error;
    if (disk) {
        if (!fs_ready()) return -ERROR_IO;
        int size=fs_file_size(name);if (size<0) return -ERROR_NOT_FOUND;
        *result=(struct notos_file_info){disk_inode(name),(u32)size,NOTOS_FILE_REGULAR,1,0444};
        return 0;
    }
    struct vfs_node *node=find(name);if (!node) return -ERROR_NOT_FOUND;
    vfs_node_info(node,result);return 0;
}
struct vfs_counts vfs_counts(void) {
    struct vfs_counts counts={0,0,page_count,0};
    for (u32 i=0;i<VFS_NODE_COUNT;i++) if (nodes[i].used) {
        counts.nodes++;counts.references+=nodes[i].references;
        if (!nodes[i].disk && nodes[i].linked) counts.files++;
    }
    return counts;
}
void vfs_status(void) {
    struct vfs_counts c=vfs_counts();
    console_puts("VFS: files=");console_uint(c.files);console_puts(" nodes=");console_uint(c.nodes);
    console_puts(" pages=");console_uint(c.pages);console_puts(" refs=");console_uint(c.references);console_putc('\n');
}
