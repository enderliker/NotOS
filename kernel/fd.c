/* No CPU, syscall or scheduling policy here; buffers and paths are trusted. */
#include "fd.h"
#include "physical.h"
#include "socket_stack.h"
#include "vfs.h"
#include "syscall_numbers.h"
#define DESCRIPTION_COUNT 64u
#define PIPE_CAPACITY 16u
#define PIPE_BUFFER_BYTES 4096u
enum file_kind { FILE_REGULAR,FILE_INPUT,FILE_OUTPUT,FILE_CONSOLE,FILE_NULL,FILE_SOCKET,FILE_PIPE };
struct pipe_state { u32 page,head,count,readers,writers; };
struct open_file {
    u32 references,flags,offset;
    enum file_kind kind;
    struct vfs_node *node;
    int socket;
};
static struct open_file descriptions[DESCRIPTION_COUNT];
static struct pipe_state pipes[PIPE_CAPACITY];
static u8 *pipe_bytes(const struct pipe_state *pipe) {return (u8 *)(unsigned long)pipe->page;}
static struct open_file *get(struct fd_table *table,int fd) {
    if (fd<0 || (u32)fd>=NOTOS_FD_COUNT || !table->slots[fd]) return 0;
    return &descriptions[table->slots[fd]-1];
}
static int free_slot(struct fd_table *table) {
    for (u32 i=0;i<NOTOS_FD_COUNT;i++) if (!table->slots[i]) return (int)i;
    return -1;
}
static int device(const char *path,enum file_kind *kind,u32 *inode) {
    if (equal(path,"/dev/stdin")) {*kind=FILE_INPUT;*inode=1;}
    else if (equal(path,"/dev/stdout")) {*kind=FILE_OUTPUT;*inode=2;}
    else if (equal(path,"/dev/stderr")) {*kind=FILE_OUTPUT;*inode=3;}
    else if (equal(path,"/dev/console")) {*kind=FILE_CONSOLE;*inode=4;}
    else if (equal(path,"/dev/null")) {*kind=FILE_NULL;*inode=5;}
    else return 0;
    return 1;
}
int fd_open(struct fd_table *table,const char *path,u32 flags,u32 mode) {
    if ((flags&~NOTOS_O_ALL) || (flags&NOTOS_O_ACCMODE)==NOTOS_O_ACCMODE
        || ((flags&NOTOS_O_EXCL) && !(flags&NOTOS_O_CREAT))
        || ((flags&NOTOS_O_TRUNC) && (flags&NOTOS_O_ACCMODE)==NOTOS_O_RDONLY)) return -ERROR_INVALID_ARGUMENT;
    int slot=free_slot(table),index=-1;
    if (slot<0) return -ERROR_TOO_MANY_FILES;
    for (u32 i=0;i<DESCRIPTION_COUNT;i++) if (!descriptions[i].references) {index=(int)i;break;}
    if (index<0) return -ERROR_TOO_MANY_FILES;
    struct open_file file={.references=1,.flags=flags&(NOTOS_O_ACCMODE|NOTOS_O_APPEND|NOTOS_O_NONBLOCK)};
    u32 inode;
    if (device(path,&file.kind,&inode)) {
        if (flags&(NOTOS_O_CREAT|NOTOS_O_EXCL|NOTOS_O_TRUNC)) return -ERROR_INVALID_ARGUMENT;
        if ((file.kind==FILE_INPUT && (flags&NOTOS_O_ACCMODE)!=NOTOS_O_RDONLY)
            || (file.kind==FILE_OUTPUT && (flags&NOTOS_O_ACCMODE)!=NOTOS_O_WRONLY)) return -ERROR_ACCESS;
        file.offset=inode; /* Device identity; not a seekable offset. */
    } else {
        file.kind=FILE_REGULAR;
        int error=vfs_open(path,flags,mode,&file.node);if (error) return error;
    }
    descriptions[index]=file;table->slots[slot]=(u16)(index+1);
    table->descriptor_flags[slot]=(flags&NOTOS_O_CLOEXEC)?NOTOS_FD_CLOEXEC:0;
    return slot;
}
int fd_table_init(struct fd_table *table) {
    *table=(struct fd_table){0};
    if (fd_open(table,"/dev/stdin",NOTOS_O_RDONLY,0)!=0
        || fd_open(table,"/dev/stdout",NOTOS_O_WRONLY,0)!=1
        || fd_open(table,"/dev/stderr",NOTOS_O_WRONLY,0)!=2) {
        fd_table_destroy(table);return 0;
    }
    return 1;
}
int fd_close(struct fd_table *table,int fd) {
    struct open_file *file=get(table,fd);if (!file) return -ERROR_BAD_FD;
    table->slots[fd]=0;table->descriptor_flags[fd]=0;
    if (!--file->references) {
        if (file->kind==FILE_SOCKET) socket_release(file->socket);
        else if (file->kind==FILE_PIPE) {
            struct pipe_state *pipe=&pipes[file->offset];
            if ((file->flags&NOTOS_O_ACCMODE)==NOTOS_O_RDONLY) pipe->readers--;
            else pipe->writers--;
            if (!pipe->readers && !pipe->writers) {physical_free(pipe->page);*pipe=(struct pipe_state){0};}
        } else vfs_release(file->node);
        *file=(struct open_file){0};
    }
    return 0;
}
void fd_table_destroy(struct fd_table *table) {
    socket_resolve_cancel(table);
    for (u32 i=0;i<NOTOS_FD_COUNT;i++) if (table->slots[i]) fd_close(table,(int)i);
    *table=(struct fd_table){0};
}
int fd_pipe(struct fd_table *table,int fds[2],u32 flags) {
    if (!table || !fds || (flags&~(NOTOS_O_NONBLOCK|NOTOS_O_CLOEXEC))) return -ERROR_INVALID_ARGUMENT;
    int r_slot=-1,w_slot=-1,r_desc=-1,w_desc=-1,p_idx=-1;
    for (u32 i=0;i<NOTOS_FD_COUNT;i++) if (!table->slots[i]) {
        if (r_slot<0) r_slot=(int)i; else {w_slot=(int)i;break;}
    }
    for (u32 i=0;i<DESCRIPTION_COUNT;i++) if (!descriptions[i].references) {
        if (r_desc<0) r_desc=(int)i; else {w_desc=(int)i;break;}
    }
    for (u32 i=0;i<PIPE_CAPACITY;i++) if (!pipes[i].page) {p_idx=(int)i;break;}
    if (w_slot<0 || w_desc<0 || p_idx<0) return -ERROR_TOO_MANY_FILES;
    u32 page=physical_alloc();
    if (!page) return -ERROR_MEMORY;
    pipes[p_idx]=(struct pipe_state){.page=page,.readers=1,.writers=1};
    for (u32 i=0;i<PIPE_BUFFER_BYTES;i++) pipe_bytes(&pipes[p_idx])[i]=0;
    u32 nonblock=flags&NOTOS_O_NONBLOCK;
    u8 cloexec=(flags&NOTOS_O_CLOEXEC)?NOTOS_FD_CLOEXEC:0;
    descriptions[r_desc]=(struct open_file){.references=1,.kind=FILE_PIPE,.flags=NOTOS_O_RDONLY|nonblock,.offset=(u32)p_idx};
    descriptions[w_desc]=(struct open_file){.references=1,.kind=FILE_PIPE,.flags=NOTOS_O_WRONLY|nonblock,.offset=(u32)p_idx};
    table->slots[r_slot]=(u16)(r_desc+1);table->descriptor_flags[r_slot]=cloexec;
    table->slots[w_slot]=(u16)(w_desc+1);table->descriptor_flags[w_slot]=cloexec;
    fds[0]=r_slot;fds[1]=w_slot;
    return 0;
}
u32 fd_pipe_count(void) {
    u32 count=0;
    for (u32 i=0;i<PIPE_CAPACITY;i++) if (pipes[i].page) count++;
    return count;
}
int fd_table_spawn(struct fd_table *child,struct fd_table *parent,const struct notos_fd_map *map,u32 map_count) {
    if (!child || !parent || (map_count && !map) || map_count>NOTOS_FD_MAP_MAX) return -ERROR_INVALID_ARGUMENT;
    for (u32 i=0;i<map_count;i++) {
        if (map[i].child_fd<0 || (u32)map[i].child_fd>=NOTOS_FD_COUNT) return -ERROR_BAD_FD;
        if (map[i].parent_fd!=NOTOS_FD_MAP_CLOSE && !get(parent,map[i].parent_fd)) return -ERROR_BAD_FD;
    }
    u16 next_slots[NOTOS_FD_COUNT]={0};
    for (u32 fd=0;fd<NOTOS_FD_COUNT;fd++)
        if (parent->slots[fd] && !(parent->descriptor_flags[fd]&NOTOS_FD_CLOEXEC)) next_slots[fd]=parent->slots[fd];
    for (u32 i=0;i<map_count;i++) {
        int cfd=map[i].child_fd,pfd=map[i].parent_fd;
        next_slots[cfd]=pfd==NOTOS_FD_MAP_CLOSE?0:parent->slots[pfd];
    }
    for (u32 fd=0;fd<NOTOS_FD_COUNT;fd++) if (next_slots[fd]) descriptions[next_slots[fd]-1].references++;
    fd_table_destroy(child);
    for (u32 fd=0;fd<NOTOS_FD_COUNT;fd++) child->slots[fd]=next_slots[fd];
    return 0;
}
int fd_read(struct fd_table *table,int fd,void *buffer,u32 bytes) {
    struct open_file *file=get(table,fd);
    if (!file || (file->flags&NOTOS_O_ACCMODE)==NOTOS_O_WRONLY) return -ERROR_BAD_FD;
    if (!bytes) return 0;
    if (file->kind==FILE_REGULAR) {
        int result=vfs_read(file->node,file->offset,buffer,bytes);
        if (result>0) file->offset+=(u32)result;
        return result;
    }
    if (file->kind==FILE_PIPE) {
        struct pipe_state *pipe=&pipes[file->offset];
        if (!pipe->count) return pipe->writers?-ERROR_AGAIN:0;
        if (bytes>pipe->count) bytes=pipe->count;
        const u8 *ring=pipe_bytes(pipe);
        for (u32 i=0;i<bytes;i++) {
            ((u8 *)buffer)[i]=ring[pipe->head];
            pipe->head=(pipe->head+1)%PIPE_BUFFER_BYTES;
        }
        pipe->count-=bytes;return (int)bytes;
    }
    if (file->kind==FILE_SOCKET) return socket_read(file->socket,buffer,bytes,0);
    if (file->kind==FILE_NULL) return 0;
    if (!table->input_count) return table->input_eof?0:-ERROR_AGAIN;
    if (bytes>table->input_count) bytes=table->input_count;
    for (u32 i=0;i<bytes;i++) {
        ((u8 *)buffer)[i]=table->input[table->input_head];
        table->input_head=(table->input_head+1)%NOTOS_INPUT_BYTES;
    }
    table->input_count-=bytes;return (int)bytes;
}
int fd_write(struct fd_table *table,int fd,const void *buffer,u32 bytes) {
    struct open_file *file=get(table,fd);
    if (!file || (file->flags&NOTOS_O_ACCMODE)==NOTOS_O_RDONLY) return -ERROR_BAD_FD;
    if (file->kind==FILE_REGULAR) {
        u32 offset=file->offset;
        if (file->flags&NOTOS_O_APPEND) {struct notos_file_info info;vfs_node_info(file->node,&info);offset=info.size;}
        int result=vfs_write(file->node,offset,buffer,bytes);
        if (result>0) file->offset=offset+(u32)result;
        return result;
    }
    if (file->kind==FILE_PIPE) {
        struct pipe_state *pipe=&pipes[file->offset];
        if (!pipe->readers) return -ERROR_BROKEN_PIPE;
        if (!bytes) return 0;
        u32 available=PIPE_BUFFER_BYTES-pipe->count;
        if (!available) return -ERROR_AGAIN;
        if (bytes>available) bytes=available;
        u8 *ring=pipe_bytes(pipe);
        for (u32 i=0;i<bytes;i++) ring[(pipe->head+pipe->count+i)%PIPE_BUFFER_BYTES]=((const u8 *)buffer)[i];
        pipe->count+=bytes;return (int)bytes;
    }
    if (file->kind==FILE_SOCKET) return socket_write(file->socket,buffer,bytes);
    if (file->kind!=FILE_NULL) for (u32 i=0;i<bytes;i++) console_putc(((const char *)buffer)[i]);
    return (int)bytes;
}
int fd_seek(struct fd_table *table,int fd,int offset,u32 origin) {
    struct open_file *file=get(table,fd);if (!file) return -ERROR_BAD_FD;
    if (file->kind!=FILE_REGULAR) return -ERROR_ILLEGAL_SEEK;
    u32 base;
    if (origin==NOTOS_SEEK_SET) base=0;
    else if (origin==NOTOS_SEEK_CUR) base=file->offset;
    else if (origin==NOTOS_SEEK_END) {struct notos_file_info info;vfs_node_info(file->node,&info);base=info.size;}
    else return -ERROR_INVALID_ARGUMENT;
    struct notos_file_info info;vfs_node_info(file->node,&info);
    u32 limit=vfs_max_size(file->node);
    long long position=(long long)base+offset;
    if (position<0 || position>(info.size>limit?info.size:limit)) return -ERROR_INVALID_ARGUMENT;
    file->offset=(u32)position;return (int)position;
}
int fd_truncate(struct fd_table *table,int fd,u32 bytes) {
    struct open_file *file=get(table,fd);if (!file) return -ERROR_BAD_FD;
    if ((file->flags&NOTOS_O_ACCMODE)==NOTOS_O_RDONLY) return -ERROR_BAD_FD;
    if (file->kind!=FILE_REGULAR) return -ERROR_INVALID_ARGUMENT;
    return vfs_truncate(file->node,bytes);
}
int fd_stat(struct fd_table *table,int fd,struct notos_file_info *result) {
    struct open_file *file=get(table,fd);if (!file) return -ERROR_BAD_FD;
    if (file->kind==FILE_REGULAR) vfs_node_info(file->node,result);
    else if (file->kind==FILE_PIPE) *result=(struct notos_file_info){file->offset+8,pipes[file->offset].count,NOTOS_FILE_FIFO,1,0600,0,0,0};
    else if (file->kind==FILE_SOCKET) *result=(struct notos_file_info){0,0,NOTOS_FILE_SOCKET,1,0600,0,0,0};
    else *result=(struct notos_file_info){file->offset,0,NOTOS_FILE_CHARACTER,1,0666,0,0,0};
    return 0;
}
int fd_path_stat(const char *path,struct notos_file_info *result) {
    enum file_kind kind;u32 inode;
    if (device(path,&kind,&inode)) {*result=(struct notos_file_info){inode,0,NOTOS_FILE_CHARACTER,1,0666,0,0,0};return 0;}
    return vfs_stat(path,result);
}
int fd_dup(struct fd_table *table,int fd,int destination) {
    struct open_file *file=get(table,fd);if (!file) return -ERROR_BAD_FD;
    if (destination==-1) {destination=free_slot(table);if (destination<0) return -ERROR_TOO_MANY_FILES;}
    if (destination<0 || (u32)destination>=NOTOS_FD_COUNT) return -ERROR_BAD_FD;
    if (destination==fd) return fd;
    if (table->slots[destination]) fd_close(table,destination);
    file->references++;table->slots[destination]=table->slots[fd];table->descriptor_flags[destination]=0;
    return destination;
}
int fd_control(struct fd_table *table,int fd,u32 command,u32 argument) {
    struct open_file *file=get(table,fd);if (!file) return -ERROR_BAD_FD;
    switch (command) {
    case NOTOS_F_GETFL:return (int)file->flags;
    case NOTOS_F_SETFL:
        if (argument&~(NOTOS_O_ACCMODE|NOTOS_O_APPEND|NOTOS_O_NONBLOCK)) return -ERROR_INVALID_ARGUMENT;
        file->flags=(file->flags&~(NOTOS_O_APPEND|NOTOS_O_NONBLOCK))|(argument&(NOTOS_O_APPEND|NOTOS_O_NONBLOCK));return 0;
    case NOTOS_F_GETFD:return table->descriptor_flags[fd];
    case NOTOS_F_SETFD:
        if (argument&~NOTOS_FD_CLOEXEC) return -ERROR_INVALID_ARGUMENT;
        table->descriptor_flags[fd]=(u8)argument;return 0;
    default:return -ERROR_UNSUPPORTED;
    }
}
int fd_is_tty(struct fd_table *table,int fd) {
    struct open_file *file=get(table,fd);if (!file) return -ERROR_BAD_FD;
    return file->kind==FILE_INPUT || file->kind==FILE_OUTPUT || file->kind==FILE_CONSOLE;
}
int fd_blocking_input(struct fd_table *table,int fd) {
    struct open_file *file=get(table,fd);
    return file && (file->kind==FILE_INPUT || file->kind==FILE_CONSOLE
        || (file->kind==FILE_PIPE && (file->flags&NOTOS_O_ACCMODE)==NOTOS_O_RDONLY))
        && !(file->flags&NOTOS_O_NONBLOCK);
}
int fd_blocking_output(struct fd_table *table,int fd) {
    struct open_file *file=get(table,fd);
    return file && file->kind==FILE_PIPE && (file->flags&NOTOS_O_ACCMODE)==NOTOS_O_WRONLY
        && !(file->flags&NOTOS_O_NONBLOCK);
}
int fd_feed(struct fd_table *table,const void *buffer,u32 bytes,int eof) {
    if ((bytes && table->input_eof) || bytes>NOTOS_INPUT_BYTES-table->input_count) return -ERROR_NO_SPACE;
    for (u32 i=0;i<bytes;i++) table->input[(table->input_head+table->input_count+i)%NOTOS_INPUT_BYTES]=((const u8 *)buffer)[i];
    table->input_count+=bytes;if (eof) table->input_eof=1;
    return 0;
}
u32 fd_description_count(void) {
    u32 count=0;
    for (u32 i=0;i<DESCRIPTION_COUNT;i++) if (descriptions[i].references) count++;
    return count;
}

int fd_socket_open(struct fd_table *table,u32 flags) {
    int slot=free_slot(table),index=-1;
    if (slot<0) return -ERROR_TOO_MANY_FILES;
    for (u32 i=0;i<DESCRIPTION_COUNT;i++) if (!descriptions[i].references) {index=(int)i;break;}
    if (index<0) return -ERROR_TOO_MANY_FILES;
    int handle=socket_create();if (handle<0) return handle;
    descriptions[index]=(struct open_file){.references=1,.kind=FILE_SOCKET,.socket=handle,
        .flags=NOTOS_O_RDWR|(flags&NOTOS_O_NONBLOCK)};
    table->slots[slot]=(u16)(index+1);table->descriptor_flags[slot]=(flags&NOTOS_O_CLOEXEC)?NOTOS_FD_CLOEXEC:0;
    return slot;
}
int fd_socket_handle(struct fd_table *table,int fd) {
    struct open_file *file=get(table,fd);if (!file) return -ERROR_BAD_FD;
    return file->kind==FILE_SOCKET?file->socket:-ERROR_NOT_SOCKET;
}
int fd_nonblocking(struct fd_table *table,int fd) {
    struct open_file *file=get(table,fd);return file && (file->flags&NOTOS_O_NONBLOCK);
}

static u16 ready(struct fd_table *table,struct open_file *file) {
    if (!file) return NOTOS_POLLNVAL;
    if (file->kind==FILE_SOCKET) return socket_ready(file->socket);
    if (file->kind==FILE_PIPE) {
        const struct pipe_state *pipe=&pipes[file->offset];
        if ((file->flags&NOTOS_O_ACCMODE)==NOTOS_O_RDONLY)
            return (pipe->count?NOTOS_POLLIN:0)|(pipe->writers?0:NOTOS_POLLHUP);
        return pipe->readers?(pipe->count<PIPE_BUFFER_BYTES?NOTOS_POLLOUT:0):NOTOS_POLLERR;
    }
    if (file->kind==FILE_REGULAR || file->kind==FILE_NULL) return NOTOS_POLLIN|NOTOS_POLLOUT;
    u16 events=file->kind==FILE_OUTPUT || file->kind==FILE_CONSOLE?NOTOS_POLLOUT:0;
    if (file->kind==FILE_INPUT || file->kind==FILE_CONSOLE) {
        if (table->input_count || table->input_eof) events|=NOTOS_POLLIN;
        if (table->input_eof) events|=NOTOS_POLLHUP;
    }
    return events;
}
int fd_poll(struct fd_table *table,struct notos_pollfd *fds,u32 count) {
    int result=0;
    for (u32 i=0;i<count;i++) {
        u16 events=fds[i].fd<0?0:ready(table,get(table,fds[i].fd));
        fds[i].revents=events&(fds[i].events|NOTOS_POLL_ALWAYS);
        if (fds[i].revents) result++;
    }
    return result;
}

void fd_close_exec(struct fd_table *table) {
    for (u32 i = 0; i < NOTOS_FD_COUNT; i++)
        if (table->slots[i] && (table->descriptor_flags[i] & NOTOS_FD_CLOEXEC)) fd_close(table, (int)i);
}
int fd_readdir(struct fd_table *table, int fd, struct notos_directory_entry *entry) {
    struct open_file *file = get(table, fd);
    if (!file) return -ERROR_BAD_FD;
    if (file->kind != FILE_REGULAR) return -ERROR_NOT_DIRECTORY;
    int result = vfs_readdir(file->node, file->offset, entry);
    if (result > 0) file->offset++;
    return result;
}

