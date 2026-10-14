/* Bounded tmpfs, persistent snapshots and immutable system images. */
#include "vfs.h"
#include "system_file.h"
#include "physical.h"
#include "fs.h"
#include "platform_tls.h"
#include "syscall_numbers.h"
#define NODE_NAME_BYTES NOTOS_PATH_BYTES
#define NODE_PAGES (FS_FILE_MAX/4096u)
struct vfs_node {
    u32 used,disk,linked,references,inode,size,permissions,kind;
    u16 uid,gid;
    u64 mtime;
    char name[NODE_NAME_BYTES];
    const u8 *image;
    u32 pages[NODE_PAGES];
};
static struct vfs_node nodes[VFS_NODE_COUNT];
static u32 page_count,next_inode=16;
static u64 current_mtime(void) {
    struct utc_time utc;
    return rtc_utc(&utc) && utc.days >= 719528u ? (u64)(utc.days - 719528u) * 86400u + utc.seconds : 0;
}
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
        if (!((c>='a'&&c<='z')||(c>='A'&&c<='Z')||(c>='0'&&c<='9')||c=='.'||c=='_'||c=='-'||c=='/'))
            return -ERROR_INVALID_ARGUMENT;
    }
    if (!bytes) return -ERROR_INVALID_ARGUMENT;
    const char *component = *name;
    for (const char *cursor = *name;; cursor++) {
        if (*cursor && *cursor != '/') continue;
        u32 count = (u32)(cursor - component);
        if (!count || count > 23 || (count == 1 && component[0] == '.')
            || (count == 2 && component[0] == '.' && component[1] == '.')) return -ERROR_INVALID_ARGUMENT;
        if (!*cursor) break;
        component = cursor + 1;
    }
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
/* Picolibc's i386 ino_t is 16-bit. Keep IDs distinct across live nodes and devices. */
static u32 allocate_inode(void) {
    for (u32 attempt=0;attempt<=VFS_NODE_COUNT;attempt++) {
        if (next_inode>255u) next_inode=16;
        u32 candidate=next_inode++;int busy=0;
        for (u32 i=0;i<VFS_NODE_COUNT;i++) if (nodes[i].used && nodes[i].inode==candidate) busy=1;
        if (!busy) return candidate;
    }
    return 0;
}
u32 vfs_max_size(const struct vfs_node *node) {
    if (node && node->disk == 1 && !node->image && node->linked) return FS_FILE_MAX;
    return VFS_FILE_BYTES;
}
void vfs_node_info(const struct vfs_node *node,struct notos_file_info *result) {
    *result=(struct notos_file_info){node->inode,node->size,node->kind,node->linked,
        node->permissions,node->uid,node->gid,node->mtime};
    if (node->disk == 1 && !node->image && node->linked) {
        struct notos_file_info current;
        if (!fs_info(node->name, &current) && current.inode == node->inode) *result = current;
        else result->links = 0;
    } else if (node->disk == 2) {
        struct notos_file_info current;
        if (!vfs_stat(node->name, &current) && current.inode == node->inode) *result = current;
        else result->links = 0;
    }
}
static u32 system_inode(const char *path) {
    if (equal(path, "/system/cacert.pem")) return 6u;
    if (equal(path, "/system/sh.elf")) return 7u;
    if (equal(path, "/system/libc.elf")) return 13u;
    if (equal(path, "/system/curl.elf")) return 14u;
    return 15u;
}
int vfs_open(const char *path,u32 flags,u32 mode,struct vfs_node **result) {
    u32 image_bytes;
    const u8 *image = system_file_lookup(path, &image_bytes);
    if (image) {
        if ((flags & NOTOS_O_ACCMODE) != NOTOS_O_RDONLY || (flags & (NOTOS_O_CREAT | NOTOS_O_TRUNC))) return -ERROR_READ_ONLY;
        for (u32 i = 0; i < VFS_NODE_COUNT; i++) if (nodes[i].used && nodes[i].image == image) {
            nodes[i].references++;
            *result = &nodes[i];
            return 0;
        }
        for (u32 i = 0; i < VFS_NODE_COUNT; i++) if (!nodes[i].used) {
            nodes[i] = (struct vfs_node){.used=1,.disk=1,.linked=1,.references=1,.inode=system_inode(path),.size=image_bytes,.permissions=0444,.kind=NOTOS_FILE_REGULAR,.image=image};
            *result = &nodes[i];
            return 0;
        }
        return -ERROR_NO_SPACE;
    }
    struct notos_file_info directory;
    if (!vfs_stat(path, &directory) && directory.kind == NOTOS_FILE_DIRECTORY) {
        if ((flags & NOTOS_O_ACCMODE) != NOTOS_O_RDONLY || (flags & (NOTOS_O_CREAT | NOTOS_O_TRUNC))) return -ERROR_IS_DIRECTORY;
        if (starts(path, "/tmp/")) {
            struct vfs_node *node = find(path + 5);
            node->references++;
            *result = node;
            return 0;
        }
        for (u32 i = 0; i < VFS_NODE_COUNT; i++) if (nodes[i].used && nodes[i].disk == 2 && nodes[i].inode == directory.inode && equal(nodes[i].name, path)) {
            nodes[i].references++;
            *result = &nodes[i];
            return 0;
        }
        for (u32 i = 0; i < VFS_NODE_COUNT; i++) if (!nodes[i].used) {
            nodes[i] = (struct vfs_node){.used=1,.disk=2,.linked=1,.references=1,.inode=directory.inode,.kind=NOTOS_FILE_DIRECTORY,.permissions=directory.permissions,.uid=directory.uid,.gid=directory.gid,.mtime=directory.mtime};
            copy(nodes[i].name, path, length(path) + 1);
            *result = &nodes[i];
            return 0;
        }
        return -ERROR_NO_SPACE;
    }
    int disk;const char *name;int error=path_name(path,&disk,&name);
    if (error) return error;
    struct vfs_node *node=0;
    if (disk) {
        if (!fs_ready()) return -ERROR_IO;
        if (!(flags & NOTOS_O_CREAT) && fs_file_size(name) < 0) return -ERROR_NOT_FOUND;
        struct notos_file_info info;
        if (!fs_info(name, &info)) for (u32 i = 0; i < VFS_NODE_COUNT; i++)
            if (nodes[i].used && nodes[i].disk == 1 && !nodes[i].image && nodes[i].linked && nodes[i].inode == info.inode) node = &nodes[i];
    } else {
        node=find(name);
        if (node && (flags&(NOTOS_O_CREAT|NOTOS_O_EXCL))==(NOTOS_O_CREAT|NOTOS_O_EXCL)) return -ERROR_EXISTS;
        if (!node && !(flags&NOTOS_O_CREAT)) return -ERROR_NOT_FOUND;
    }
    if (!node && !disk) {
        char parent[NOTOS_PATH_BYTES];
        copy(parent, path, length(path) + 1);
        u32 end = length(parent);
        while (end && parent[end - 1] != '/') end--;
        parent[end > 1 ? end - 1 : 1] = 0;
        struct notos_file_info info;
        int parent_error = vfs_stat(parent, &info);
        if (parent_error) return parent_error;
        if (info.kind != NOTOS_FILE_DIRECTORY) return -ERROR_NOT_DIRECTORY;
    }
    int created=!node;
    if (created) {
        struct vfs_counts counts=vfs_counts();
        if (!disk && counts.files==VFS_FILE_COUNT) return -ERROR_NO_SPACE;
        for (u32 i=0;i<VFS_NODE_COUNT;i++) if (!nodes[i].used) {node=&nodes[i];break;}
        if (!node) return -ERROR_NO_SPACE;
        u32 inode=disk?256u:allocate_inode();
        if (!inode) return -ERROR_NO_SPACE;
        *node=(struct vfs_node){.used=1,.disk=(u32)disk,.linked=1,.inode=inode,
            .permissions=mode&0777u,.kind=NOTOS_FILE_REGULAR,.mtime=current_mtime()};
        copy(node->name,name,length(name)+1);
        if (disk) {
            error = fs_open_file(name, flags, mode);
            if (error) {dispose(node);return error;}
            struct notos_file_info info;
            fs_info(name, &info);
            node->inode = info.inode;
            node->size = info.size;
            node->permissions = info.permissions;
            node->uid = info.uid;
            node->gid = info.gid;
            node->mtime = info.mtime;
        }
    }
    if (disk && !created) {
        error = fs_open_file(name, flags, mode);
        if (error) return error;
        struct notos_file_info info;
        if (!fs_info(name, &info)) {
            node->size = info.size;
            node->mtime = info.mtime;
        }
    }
    if (!disk && (flags&NOTOS_O_TRUNC)) {
        error=vfs_truncate(node,0);
        if (error) {if (created) dispose(node);return error;}
    }
    node->references++;*result=node;
    return 0;
}
int vfs_read(struct vfs_node *node,u32 offset,void *buffer,u32 bytes) {
    if (node->kind == NOTOS_FILE_DIRECTORY) return -ERROR_IS_DIRECTORY;
    if (node->disk == 1 && !node->image && node->linked) {
        struct notos_file_info info;
        if (fs_info(node->name, &info) || info.inode != node->inode) return -ERROR_NOT_FOUND;
        return fs_read_range(node->name, offset, buffer, bytes);
    }
    if (offset>=node->size) return 0;
    if (bytes>node->size-offset) bytes=node->size-offset;
    if (node->image) { copy(buffer, node->image + offset, bytes); return (int)bytes; }
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
    if (node->kind == NOTOS_FILE_DIRECTORY) return -ERROR_IS_DIRECTORY;
    if (node->image) return -ERROR_READ_ONLY;
    if (!bytes) return 0;
    if (node->disk && node->linked) return fs_write_range(node->name, node->inode, offset, buffer, bytes);
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
    node->mtime = current_mtime();
    return (int)bytes;
}
int vfs_truncate(struct vfs_node *node,u32 bytes) {
    if (node->kind == NOTOS_FILE_DIRECTORY) return -ERROR_IS_DIRECTORY;
    if (node->image) return -ERROR_READ_ONLY;
    if (node->disk && node->linked) return fs_truncate_file(node->name, node->inode, bytes);
    if (bytes>VFS_FILE_BYTES) return -ERROR_FILE_TOO_LARGE;
    if (bytes>node->size) {int error=reserve(node,bytes);if (error) return error;}
    if (bytes<node->size) {
        u32 keep=(bytes+4095)/4096;
        for (u32 i=keep;i<NODE_PAGES;i++) if (node->pages[i]) {
            physical_free(node->pages[i]);node->pages[i]=0;page_count--;
        }
        if (bytes&4095) for (u32 i=bytes&4095;i<4096;i++) page_bytes(node->pages[bytes/4096])[i]=0;
    }
    node->size=bytes;
    node->mtime=current_mtime();
    return 0;
}
int vfs_unlink(const char *path) {
    if (starts(path, "/system/") || starts(path, "/dev/")) return -ERROR_READ_ONLY;
    struct notos_file_info directory;
    if (!vfs_stat(path, &directory) && directory.kind == NOTOS_FILE_DIRECTORY) return -ERROR_IS_DIRECTORY;
    int disk;const char *name;int error=path_name(path,&disk,&name);
    if (error) return error;
    if (disk) {
        struct notos_file_info info;
        error = fs_info(name, &info);
        if (error) return error;
        if (info.kind == NOTOS_FILE_DIRECTORY) return -ERROR_IS_DIRECTORY;
        struct vfs_node *opened = 0;
        for (u32 i = 0; i < VFS_NODE_COUNT; i++) if (nodes[i].used && nodes[i].disk == 1
            && !nodes[i].image && nodes[i].linked && nodes[i].inode == info.inode) opened = &nodes[i];
        struct vfs_node snapshot = {0};
        if (opened) {
            error = reserve(&snapshot, info.size);
            if (error) return error;
            for (u32 offset = 0; offset < info.size; offset += 4096) {
                u32 bytes = info.size - offset;
                if (bytes > 4096) bytes = 4096;
                if (fs_read_range(name, offset, page_bytes(snapshot.pages[offset / 4096]), bytes) != (int)bytes) {
                    dispose(&snapshot); return -ERROR_IO;
                }
            }
        }
        error = fs_remove(name, 0);
        if (error) {dispose(&snapshot); return error;}
        if (opened) {
            copy(opened->pages, snapshot.pages, sizeof(snapshot.pages));
            opened->size = info.size;
            opened->uid = info.uid;
            opened->gid = info.gid;
            opened->mtime = info.mtime;
            opened->linked = 0;
        }
        return 0;
    }
    struct vfs_node *node=find(name);if (!node) return -ERROR_NOT_FOUND;
    if (node->kind == NOTOS_FILE_DIRECTORY) return -ERROR_IS_DIRECTORY;
    node->linked=0;if (!node->references) dispose(node);
    return 0;
}
int vfs_rename(const char *old_path,const char *new_path) {
    if (!old_path || !new_path) return -ERROR_INVALID_ARGUMENT;
    if (equal(old_path, "/") || equal(old_path, "/tmp") || equal(old_path, "/disk")
        || equal(new_path, "/") || equal(new_path, "/tmp") || equal(new_path, "/disk")) return -ERROR_ACCESS;
    if (equal(old_path, "/system") || starts(old_path, "/system/") || equal(old_path, "/dev") || starts(old_path, "/dev/")
        || equal(new_path, "/system") || starts(new_path, "/system/") || equal(new_path, "/dev") || starts(new_path, "/dev/"))
        return -ERROR_READ_ONLY;
    int old_disk, new_disk;
    const char *old_name, *new_name;
    int error = path_name(old_path, &old_disk, &old_name);
    if (error) return error;
    error = path_name(new_path, &new_disk, &new_name);
    if (error) return error;
    struct notos_file_info old_info;
    error = vfs_stat(old_path, &old_info);
    if (error) return error;
    if (old_disk != new_disk) return -ERROR_CROSS_DEVICE;
    if (equal(old_name, new_name)) return 0;
    u32 old_len = length(old_name), new_len = length(new_name);
    if (starts(new_name, old_name) && new_name[old_len] == '/') return -ERROR_INVALID_ARGUMENT;
    char parent[NOTOS_PATH_BYTES];
    copy(parent, new_path, length(new_path) + 1);
    u32 end = length(parent);
    while (end && parent[end - 1] != '/') end--;
    parent[end > 1 ? end - 1 : 1] = 0;
    struct notos_file_info parent_info;
    error = vfs_stat(parent, &parent_info);
    if (error) return error;
    if (parent_info.kind != NOTOS_FILE_DIRECTORY) return -ERROR_NOT_DIRECTORY;
    if (old_disk) {
        u32 old_path_len = length(old_path), new_path_len = length(new_path);
        for (u32 i = 0; i < VFS_NODE_COUNT; i++) {
            if (!nodes[i].used || nodes[i].disk != 2 || !nodes[i].linked) continue;
            if (equal(nodes[i].name, old_path) || (starts(nodes[i].name, old_path) && nodes[i].name[old_path_len] == '/')) {
                u32 suffix = length(nodes[i].name) - old_path_len;
                if (new_path_len + suffix >= NODE_NAME_BYTES) return -ERROR_NAME_TOO_LONG;
            }
        }
        struct notos_file_info new_info = {0};
        int has_dst = (fs_info(new_name, &new_info) == 0);
        struct vfs_node *opened_dst = 0;
        if (has_dst && new_info.kind != NOTOS_FILE_DIRECTORY && new_info.inode != old_info.inode) {
            for (u32 i = 0; i < VFS_NODE_COUNT; i++) if (nodes[i].used && nodes[i].disk == 1
                && !nodes[i].image && nodes[i].linked && nodes[i].inode == new_info.inode) opened_dst = &nodes[i];
        }
        struct vfs_node snapshot = {0};
        if (opened_dst) {
            error = reserve(&snapshot, new_info.size);
            if (error) return error;
            for (u32 offset = 0; offset < new_info.size; offset += 4096) {
                u32 bytes = new_info.size - offset;
                if (bytes > 4096) bytes = 4096;
                if (fs_read_range(new_name, offset, page_bytes(snapshot.pages[offset / 4096]), bytes) != (int)bytes) {
                    dispose(&snapshot); return -ERROR_IO;
                }
            }
        }
        error = fs_rename(old_name, new_name);
        if (error) {dispose(&snapshot); return error;}
        if (opened_dst) {
            copy(opened_dst->pages, snapshot.pages, sizeof(snapshot.pages));
            opened_dst->size = new_info.size;
            opened_dst->uid = new_info.uid;
            opened_dst->gid = new_info.gid;
            opened_dst->mtime = new_info.mtime;
            opened_dst->linked = 0;
        }
        if (has_dst && new_info.inode != old_info.inode) {
            for (u32 i = 0; i < VFS_NODE_COUNT; i++)
                if (nodes[i].used && nodes[i].disk == 2 && nodes[i].linked && nodes[i].inode == new_info.inode) nodes[i].linked = 0;
        }
        for (u32 i = 0; i < VFS_NODE_COUNT; i++) {
            if (!nodes[i].used || !nodes[i].linked) continue;
            if (nodes[i].disk == 1 && !nodes[i].image) {
                if (equal(nodes[i].name, old_name) || (starts(nodes[i].name, old_name) && nodes[i].name[old_len] == '/')) {
                    char updated[NODE_NAME_BYTES];
                    u32 suffix = length(nodes[i].name) - old_len;
                    copy(updated, new_name, new_len);
                    copy(updated + new_len, nodes[i].name + old_len, suffix + 1);
                    copy(nodes[i].name, updated, new_len + suffix + 1);
                }
            } else if (nodes[i].disk == 2) {
                if (equal(nodes[i].name, old_path) || (starts(nodes[i].name, old_path) && nodes[i].name[old_path_len] == '/')) {
                    char updated[NODE_NAME_BYTES];
                    u32 suffix = length(nodes[i].name) - old_path_len;
                    copy(updated, new_path, new_path_len);
                    copy(updated + new_path_len, nodes[i].name + old_path_len, suffix + 1);
                    copy(nodes[i].name, updated, new_path_len + suffix + 1);
                }
            }
        }
        return 0;
    }
    struct vfs_node *src = find(old_name);
    if (!src) return -ERROR_NOT_FOUND;
    for (u32 i = 0; i < VFS_NODE_COUNT; i++) {
        if (!nodes[i].used || nodes[i].disk || !nodes[i].linked) continue;
        if (equal(nodes[i].name, old_name) || (starts(nodes[i].name, old_name) && nodes[i].name[old_len] == '/')) {
            u32 suffix = length(nodes[i].name) - old_len;
            if (new_len + suffix >= NODE_NAME_BYTES) return -ERROR_NAME_TOO_LONG;
        }
    }
    struct vfs_node *dst = find(new_name);
    if (dst) {
        if (src->kind == NOTOS_FILE_DIRECTORY && dst->kind != NOTOS_FILE_DIRECTORY) return -ERROR_NOT_DIRECTORY;
        if (src->kind != NOTOS_FILE_DIRECTORY && dst->kind == NOTOS_FILE_DIRECTORY) return -ERROR_IS_DIRECTORY;
        if (dst->kind == NOTOS_FILE_DIRECTORY) {
            for (u32 i = 0; i < VFS_NODE_COUNT; i++) {
                if (!nodes[i].used || nodes[i].disk || !nodes[i].linked) continue;
                if (starts(nodes[i].name, new_name) && nodes[i].name[new_len] == '/') return -ERROR_NOT_EMPTY;
            }
        }
        dst->linked = 0;
        if (!dst->references) dispose(dst);
    }
    for (u32 i = 0; i < VFS_NODE_COUNT; i++) {
        if (!nodes[i].used || nodes[i].disk || !nodes[i].linked) continue;
        if (equal(nodes[i].name, old_name) || (starts(nodes[i].name, old_name) && nodes[i].name[old_len] == '/')) {
            char updated[NODE_NAME_BYTES];
            u32 suffix = length(nodes[i].name) - old_len;
            copy(updated, new_name, new_len);
            copy(updated + new_len, nodes[i].name + old_len, suffix + 1);
            copy(nodes[i].name, updated, new_len + suffix + 1);
        }
    }
    src->mtime = current_mtime();
    return 0;
}
int vfs_stat(const char *path,struct notos_file_info *result) {
    const char *roots[] = {"/", "/tmp", "/disk", "/dev", "/system"};
    for (u32 i = 0; i < 5; i++) if (equal(path, roots[i])) {
        *result = (struct notos_file_info){8 + i,0,NOTOS_FILE_DIRECTORY,1,0755,0,0,0};
        return 0;
    }
    u32 image_bytes;
    if (system_file_lookup(path, &image_bytes)) {
        *result = (struct notos_file_info){system_inode(path),image_bytes,NOTOS_FILE_REGULAR,1,0444,0,0,0};
        return 0;
    }
    int disk;const char *name;int error=path_name(path,&disk,&name);
    if (error) return error;
    if (disk) {
        if (!fs_ready()) return -ERROR_IO;
        return fs_info(name, result);
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

int vfs_entry(u32 index, char name[24], u32 *bytes) {
    if (index >= VFS_NODE_COUNT || !name || !bytes) return 0;
    const struct vfs_node *node = &nodes[index];
    if (!node->used || node->disk || !node->linked || node->kind == NOTOS_FILE_DIRECTORY) return 0;
    for (const char *s = node->name; *s; s++) if (*s == '/') return 0;
    copy(name, node->name, length(node->name) + 1);
    *bytes = node->size;
    return 1;
}

int vfs_mkdir(const char *path, u32 mode) {
    struct notos_file_info info;
    if (!vfs_stat(path, &info)) return -ERROR_EXISTS;
    int disk;
    const char *name;
    int error = path_name(path, &disk, &name);
    if (error) return error;
    if (disk) return fs_mkdir(name, mode);
    char parent[NOTOS_PATH_BYTES];
    copy(parent, path, length(path) + 1);
    u32 end = length(parent);
    while (end && parent[end - 1] != '/') end--;
    parent[end > 1 ? end - 1 : 1] = 0;
    error = vfs_stat(parent, &info);
    if (error) return error;
    if (info.kind != NOTOS_FILE_DIRECTORY) return -ERROR_NOT_DIRECTORY;
    if (vfs_counts().files == VFS_FILE_COUNT) return -ERROR_NO_SPACE;
    for (u32 i = 0; i < VFS_NODE_COUNT; i++) if (!nodes[i].used) {
        u32 inode = allocate_inode();
        if (!inode) return -ERROR_NO_SPACE;
        nodes[i] = (struct vfs_node){.used=1,.linked=1,.inode=inode,.kind=NOTOS_FILE_DIRECTORY,.permissions=mode & 0777,.mtime=current_mtime()};
        copy(nodes[i].name, name, length(name) + 1);
        return 0;
    }
    return -ERROR_NO_SPACE;
}

int vfs_rmdir(const char *path) {
    if (equal(path, "/") || equal(path, "/tmp") || equal(path, "/disk")
        || equal(path, "/system") || equal(path, "/dev")) return -ERROR_ACCESS;
    int disk;
    const char *name;
    int error = path_name(path, &disk, &name);
    if (error) return error;
    if (disk) return fs_remove(name, 1);
    struct vfs_node *node = find(name);
    if (!node) return -ERROR_NOT_FOUND;
    if (node->kind != NOTOS_FILE_DIRECTORY) return -ERROR_NOT_DIRECTORY;
    u32 bytes = length(name);
    for (u32 i = 0; i < VFS_NODE_COUNT; i++) {
        if (!nodes[i].used || nodes[i].disk || !nodes[i].linked) continue;
        if (starts(nodes[i].name, name) && nodes[i].name[bytes] == '/') return -ERROR_NOT_EMPTY;
    }
    node->linked = 0;
    if (!node->references) dispose(node);
    return 0;
}

int vfs_directory_entry(const char *path, u32 index, struct notos_directory_entry *entry) {
    struct notos_file_info directory;
    int error = vfs_stat(path, &directory);
    if (error) return error;
    if (directory.kind != NOTOS_FILE_DIRECTORY) return -ERROR_NOT_DIRECTORY;
    *entry = (struct notos_directory_entry){0};
    const char *root[] = {"tmp", "disk", "dev", "system"};
    const char *devices[] = {"stdin", "stdout", "stderr", "console", "null"};
    const char *images[] = {"cacert.pem", "libc.elf", "curl.elf", "sh.elf"};
    const char **names = 0;
    u32 count = 0;
    if (equal(path, "/")) {names = root; count = 4;}
    else if (equal(path, "/dev")) {names = devices; count = 5;}
    else if (equal(path, "/system")) {names = images; count = 4;}
    if (names) {
        if (index >= count) return 0;
        copy(entry->name, names[index], length(names[index]) + 1);
        entry->kind = equal(path, "/") ? NOTOS_FILE_DIRECTORY : equal(path, "/dev") ? NOTOS_FILE_CHARACTER : NOTOS_FILE_REGULAR;
        char child[NOTOS_PATH_BYTES];
        u32 bytes = length(path);
        copy(child, path, bytes);
        if (bytes > 1) child[bytes++] = '/';
        copy(child + bytes, entry->name, length(entry->name) + 1);
        struct notos_file_info info;
        if (!vfs_stat(child, &info)) {entry->inode = info.inode; entry->size = info.size;}
        return 1;
    }
    if (equal(path, "/disk") || starts(path, "/disk/")) {
        struct notos_file_info info;
        int result = fs_entry(equal(path, "/disk") ? "" : path + 6, index, entry->name, &info);
        if (result > 0) {entry->inode = info.inode; entry->kind = info.kind; entry->size = info.size;}
        return result;
    }
    if (!equal(path, "/tmp") && !starts(path, "/tmp/")) return -ERROR_NOT_FOUND;
    const char *prefix = equal(path, "/tmp") ? "" : path + 5;
    u32 bytes = length(prefix);
    u32 position = 0;
    for (u32 i = 0; i < VFS_NODE_COUNT; i++) {
        const struct vfs_node *node = &nodes[i];
        if (!node->used || node->disk || !node->linked) continue;
        if (bytes && (!starts(node->name, prefix) || node->name[bytes] != '/')) continue;
        const char *name = node->name + (bytes ? bytes + 1 : 0);
        int nested = 0;
        for (const char *cursor = name; *cursor; cursor++) if (*cursor == '/') nested = 1;
        if (nested || position++ != index) continue;
        copy(entry->name, name, length(name) + 1);
        entry->inode = node->inode; entry->size = node->size; entry->kind = node->kind;
        return 1;
    }
    return 0;
}

int vfs_readdir(struct vfs_node *node, u32 index, struct notos_directory_entry *entry) {
    if (node->kind != NOTOS_FILE_DIRECTORY) return -ERROR_NOT_DIRECTORY;
    if (!node->disk) {
        if (!node->linked) return -ERROR_NOT_FOUND;
        char path[NOTOS_PATH_BYTES] = "/tmp/";
        copy(path + 5, node->name, length(node->name) + 1);
        return vfs_directory_entry(path, index, entry);
    }
    struct notos_file_info info;
    int error = vfs_stat(node->name, &info);
    if (error) return error;
    if (info.inode != node->inode) return -ERROR_NOT_FOUND;
    return vfs_directory_entry(node->name, index, entry);
}
