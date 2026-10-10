/* Per-process PAE tables and zero-filled frames. Ownership is recovered by walking tables. */
#include "space.h"
#include "physical.h"
#include "paging.h"
#include "elf.h"
#define USER_FIRST_DIRECTORY ((USER_BASE>>21)&511u)
#define USER_DIRECTORY_COUNT (USER_SIZE/(4096u*512u))
static u32 zero_frame(void) {
    u32 physical=physical_alloc();
    if (physical) {
        volatile u32 *page=(volatile u32 *)physical;
        for (unsigned i=0;i<1024;i++) page[i]=0;
    }
    return physical;
}
static struct page_entry *page_table(const struct user_space *space,u32 address) {
    if (!space || !space->directory || address<USER_BASE || address>=USER_BASE+USER_SIZE) return 0;
    const struct page_entry *directory=(const struct page_entry *)space->directory;
    u32 entry=directory[(address>>21)&511].low;
    return (entry&5)==5?(struct page_entry *)(entry&~4095u):0;
}
static u32 frame_at(const struct user_space *space,u32 address) {
    const struct page_entry *table=page_table(space,address);
    if (!table) return 0;
    u32 entry=table[(address>>12)&511].low;
    return (entry&5)==5?entry&~4095u:0;
}
static void invalidate_if_active(const struct user_space *space,u32 address) {
    if (paging_current_root()==space->root) __asm__ volatile("invlpg (%0)"::"r"(address):"memory");
}
static int map_page(struct user_space *space,u32 address,int writable,int executable) {
    if (address<USER_BASE || address>=USER_BASE+USER_SIZE || (address&4095) || (writable && executable)) return 0;
    struct page_entry *directory=(struct page_entry *)space->directory;
    unsigned index=(address>>21)&511;
    struct page_entry *table=page_table(space,address);
    int created=0;
    if (!table) {
        u32 physical=zero_frame();
        if (!physical) return 0;
        table=(struct page_entry *)physical;
        directory[index]=(struct page_entry){physical|7u,0};
        created=1;
    }
    struct page_entry *entry=&table[(address>>12)&511];
    if (entry->low&1) return 0;
    u32 physical=zero_frame();
    if (!physical) {
        if (created) {
            directory[index]=(struct page_entry){0,0};
            physical_free((u32)table);
        }
        return 0;
    }
    page_entry_set(entry,physical,5u|(writable?2u:0u),executable);
    invalidate_if_active(space,address);
    return 1;
}
static void unmap_page(struct user_space *space,u32 address) {
    struct page_entry *table=page_table(space,address);
    if (!table) return;
    struct page_entry *entry=&table[(address>>12)&511];
    if (!(entry->low&1)) return;
    u32 physical=entry->low&~4095u;
    *entry=(struct page_entry){0,0};
    invalidate_if_active(space,address);
    physical_free(physical);
    for (unsigned i=0;i<512;i++) if (table[i].low&1) return;
    struct page_entry *directory=(struct page_entry *)space->directory;
    directory[(address>>21)&511]=(struct page_entry){0,0};
    physical_free((u32)table);
}
static int map_range(struct user_space *space,u32 low,u32 high,int writable,int executable) {
    u32 cursor=low;
    while (cursor<high && map_page(space,cursor,writable,executable)) cursor+=4096;
    if (cursor==high) return 1;
    while (cursor>low) {
        cursor-=4096;
        unmap_page(space,cursor);
    }
    return 0;
}
static int space_initialize(struct user_space *space) {
    *space=(struct user_space){0};
    space->root=zero_frame();
    space->directory=zero_frame();
    if (!space->root || !space->directory || !paging_copy_kernel_directory(space->root,space->directory)) {
        space_destroy(space);
        return 0;
    }
    space->heap_end=USER_HEAP_BASE;
    return 1;
}
int space_destroy(struct user_space *space) {
    if (!space || (space->root && paging_current_root()==space->root)) return 0;
    int ok=1;
    if (space->directory) {
        struct page_entry *directory=(struct page_entry *)space->directory;
        for (unsigned n=0;n<USER_DIRECTORY_COUNT;n++) {
            u32 entry=directory[USER_FIRST_DIRECTORY+n].low;
            if (!(entry&1)) continue;
            struct page_entry *table=(struct page_entry *)(entry&~4095u);
            for (unsigned i=0;i<512;i++)
                if ((table[i].low&1) && !physical_free(table[i].low&~4095u)) ok=0;
            if (!physical_free((u32)table)) ok=0;
        }
        if (!physical_free(space->directory)) ok=0;
    }
    if (space->root && !physical_free(space->root)) ok=0;
    *space=(struct user_space){0};
    return ok;
}
int space_create(struct user_space *space,const u8 *image,unsigned bytes) {
    if (!space || space->root || space->directory || !image || !bytes || bytes>4096 || !space_initialize(space)) return 0;
    if (!map_page(space,USER_BASE,0,1) || !map_page(space,USER_BASE+4096,1,0)
        || !map_page(space,USER_BASE+12288,1,0)) {
        space_destroy(space);
        return 0;
    }
    space->table=(u32)page_table(space,USER_BASE);
    space->code=frame_at(space,USER_BASE);
    space->data=frame_at(space,USER_BASE+4096);
    space->stack=frame_at(space,USER_BASE+12288);
    space->entry=USER_BASE;
    space->stack_low=USER_BASE+12288;
    space->stack_top=USER_BASE+16384;
    u8 *code=(u8 *)space->code;
    for (unsigned i=0;i<bytes;i++) code[i]=image[i];
    return 1;
}
int space_create_elf(struct user_space *space,const u8 *image,u32 bytes) {
    struct elf_plan plan;
    if (!space || space->root || space->directory || !elf_validate(image,bytes,&plan) || !space_initialize(space)) return 0;
    for (u32 i=0;i<plan.count;i++) {
        const struct elf_segment *segment=&plan.segments[i];
        u32 low=segment->address&~4095u;
        u32 high=(segment->address+segment->memory_bytes+4095u)&~4095u;
        if (!map_range(space,low,high,(segment->flags&2)!=0,(segment->flags&1)!=0)) {
            space_destroy(space);
            return 0;
        }
        for (u32 copied=0;copied<segment->file_bytes;) {
            u32 address=segment->address+copied,offset=address&4095u;
            u32 n=4096-offset;
            if (n>segment->file_bytes-copied) n=segment->file_bytes-copied;
            u8 *destination=(u8 *)(frame_at(space,address)+offset);
            for (u32 j=0;j<n;j++) destination[j]=image[segment->offset+copied+j];
            copied+=n;
        }
    }
    if (!map_range(space,USER_STACK_BASE,USER_STACK_LIMIT,1,0)) {
        space_destroy(space);
        return 0;
    }
    space->entry=plan.entry;
    space->stack_low=USER_STACK_BASE;
    space->stack_top=USER_STACK_LIMIT;
    space->table=(u32)page_table(space,USER_BASE);
    return 1;
}
int space_range(const struct user_space *space,u32 address,u32 bytes,int writable) {
    if (!space || !space->root) return 0;
    if (!bytes) return 1;
    if (address<USER_BASE || address>=USER_BASE+USER_SIZE || bytes>USER_BASE+USER_SIZE-address) return 0;
    const struct page_entry *directory=(const struct page_entry *)space->directory;
    while (bytes) {
        u32 within=address&0x1fffffu,n=0x200000u-within;
        if (n>bytes) n=bytes;
        if (!user_memory_check(page_table(space,address),directory[(address>>21)&511].low,USER_BASE+within,n,writable)) return 0;
        address+=n;
        bytes-=n;
    }
    return 1;
}
int space_copy_to_user(struct user_space *space,u32 address,const void *data,u32 bytes) {
    if (!space_range(space,address,bytes,1)) return 0;
    const u8 *source=data;
    while (bytes) {
        u32 offset=address&4095u,n=4096-offset;
        if (n>bytes) n=bytes;
        u8 *destination=(u8 *)(frame_at(space,address)+offset);
        for (u32 i=0;i<n;i++) destination[i]=source[i];
        source+=n;address+=n;bytes-=n;
    }
    return 1;
}
int space_arguments(struct user_space *space,const char *name,const char *argument) {
    if (!space || !space->root || !name || !*name || length(name)>64
        || (argument && length(argument)>64)) return 0;
    u32 argc=argument?2:1,cursor=space->stack_top;
    cursor-=length(name)+1;u32 name_address=cursor;
    if (!space_copy_to_user(space,cursor,name,length(name)+1)) return 0;
    u32 argument_address=0;
    if (argument) {
        cursor-=length(argument)+1;argument_address=cursor;
        if (!space_copy_to_user(space,cursor,argument,length(argument)+1)) return 0;
    }
    u32 words[7]={argc,name_address,0,0,0,0,0};
    if (argument) words[2]=argument_address;
    /* argc, argv pointers+NULL, empty envp, AT_NULL pair. */
    u32 bytes=(argc+5)*sizeof(u32),stack=(cursor-bytes)&~15u;
    if (!space_copy_to_user(space,stack,words,bytes)) return 0;
    space->initial_stack=stack;
    return 1;
}
u32 space_brk(struct user_space *space,u32 requested) {
    if (!space || !space->root) return 0;
    u32 previous=space->heap_end;
    if (!requested || requested<USER_HEAP_BASE || requested>USER_HEAP_LIMIT) return previous;
    u32 old_top=(previous+4095)&~4095u,new_top=(requested+4095)&~4095u;
    if (new_top>old_top && !map_range(space,old_top,new_top,1,0)) return previous;
    while (old_top>new_top) {
        old_top-=4096;
        unmap_page(space,old_top);
    }
    space->heap_end=requested;
    return requested;
}
u32 space_mmap(struct user_space *space,u32 bytes) {
    if (!space || !space->root || !bytes || bytes>USER_ANON_LIMIT-USER_ANON_BASE) return 0;
    u32 span=(bytes+4095)&~4095u,available=0;
    for (u32 address=USER_ANON_BASE;address<USER_ANON_LIMIT;address+=4096) {
        available=frame_at(space,address)?0:available+4096;
        if (available==span) {
            u32 low=address+4096-span;
            return map_range(space,low,address+4096,1,0)?low:0;
        }
    }
    return 0;
}
int space_munmap(struct user_space *space,u32 address,u32 bytes) {
    if (!space || !space->root || !bytes || (address&4095) || address<USER_ANON_BASE
        || address>=USER_ANON_LIMIT || bytes>USER_ANON_LIMIT-address) return 0;
    u32 high=address+((bytes+4095)&~4095u);
    for (u32 cursor=address;cursor<high;cursor+=4096) unmap_page(space,cursor);
    return 1;
}
