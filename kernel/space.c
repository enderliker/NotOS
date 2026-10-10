#include "space.h"
#include "physical.h"
#include "paging.h"
static u32 zero_frame(void) {
    u32 physical=physical_alloc();
    if(physical) {volatile u32 *page=(volatile u32 *)physical;for(unsigned i=0;i<1024;i++) page[i]=0;}
    return physical;
}
int space_destroy(struct user_space *s) {
    if(s->root && paging_current_root()==s->root) return 0;
    int ok=1;
    if(s->stack&&!physical_free(s->stack)) ok=0;
    if(s->data&&!physical_free(s->data)) ok=0;
    if(s->code&&!physical_free(s->code)) ok=0;
    if(s->table&&!physical_free(s->table)) ok=0;
    if(s->directory&&!physical_free(s->directory)) ok=0;
    if(s->root&&!physical_free(s->root)) ok=0;
    s->root=s->directory=s->table=s->code=s->data=s->stack=0;return ok;
}
int space_create(struct user_space *s,const u8 *image,unsigned bytes) {
    s->root=s->directory=s->table=s->code=s->data=s->stack=0;
    if(!image||!bytes||bytes>4096) return 0;
    s->root=zero_frame();s->directory=zero_frame();s->table=zero_frame();s->code=zero_frame();s->data=zero_frame();s->stack=zero_frame();
    if(!s->root||!s->directory||!s->table||!s->code||!s->data||!s->stack) {space_destroy(s);return 0;}
    if(!paging_copy_kernel_directory(s->root,s->directory)) {space_destroy(s);return 0;}
    struct page_entry *directory=(struct page_entry *)s->directory,*table=(struct page_entry *)s->table;
    directory[(USER_BASE>>21)&511]=(struct page_entry){s->table|7u,0};
    page_entry_set(&table[0],s->code,5,1);
    page_entry_set(&table[1],s->data,7,0);page_entry_set(&table[3],s->stack,7,0);
    u8 *code=(u8 *)s->code;
    for(unsigned i=0;i<bytes;i++) code[i]=image[i];
    return 1;
}
int space_range(const struct user_space *s,u32 address,u32 bytes,int writable) {
    if(!s||!s->root||!s->table) return 0;
    const struct page_entry *directory=(const struct page_entry *)s->directory;
    return user_memory_check((const struct page_entry *)s->table,directory[(USER_BASE>>21)&511].low,address,bytes,writable);
}
