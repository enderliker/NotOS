/* Bounded first-fit heap with aligned headers, split and adjacent coalescing. */
#include "heap_core.h"
#define MAGIC 0x48454150u
#define ALIGNMENT 16u
struct heap_block {u32 size,requested,magic,free;struct heap_block *next,*prev;};
#define HEADER ((sizeof(struct heap_block)+ALIGNMENT-1)&~(ALIGNMENT-1))
static unsigned long address(const void *p) {return (unsigned long)p;}
static u8 *payload(struct heap_block *b) {return (u8 *)b+HEADER;}
static void initialize(struct heap_block *b,u32 bytes,struct heap_block *prev,struct heap_block *next) {
    b->size=bytes;b->requested=0;b->magic=MAGIC;b->free=1;b->prev=prev;b->next=next;
}
int heap_pool_init(struct heap_pool *h,void *arena,u32 bytes) {
    if(!h||!arena||(address(arena)&15)||bytes<HEADER+ALIGNMENT||address(arena)+bytes<address(arena)) return 0;
    bytes&=~15u;
    h->base=arena;h->bytes=bytes;h->head=arena;
    initialize(h->head,bytes-HEADER,0,0);return 1;
}
int heap_valid(const struct heap_pool *h) {
    if(!h||!h->base||!h->head||h->bytes<HEADER+ALIGNMENT||h->head!=(struct heap_block *)h->base) return 0;
    unsigned long base=address(h->base),end=base+h->bytes;
    if(end<base) return 0;
    struct heap_block *previous=0;
    unsigned budget=h->bytes/(HEADER+ALIGNMENT)+1;
    for(struct heap_block *b=h->head;b;b=b->next) {
        unsigned long at=address(b);
        if(!budget--||at<base||at>end-HEADER||(at&15)) return 0;
        if(b->magic!=MAGIC||b->free>1||b->size<16||(b->size&15)||b->size>end-at-HEADER) return 0;
        if(b->prev!=previous||b->requested>b->size||(b->free&&b->requested)) return 0;
        unsigned long next=at+HEADER+b->size;
        if(b->next) {if(address(b->next)!=next||next>end-HEADER) return 0;}
        else if(next!=end) return 0;
        previous=b;
    }
    return 1;
}
void *heap_take(struct heap_pool *h,u32 bytes) {
    if(!bytes||!heap_valid(h)||bytes>h->bytes-HEADER||bytes>0xfffffff0u) return 0;
    u32 need=(bytes+15)&~15u;
    for(struct heap_block *b=h->head;b;b=b->next) if(b->free&&b->size>=need) {
        if(b->size-need>=HEADER+ALIGNMENT) {
            struct heap_block *split=(struct heap_block *)(payload(b)+need);
            initialize(split,b->size-need-HEADER,b,b->next);
            if(b->next) b->next->prev=split;
            b->next=split;b->size=need;
        }
        b->free=0;b->requested=bytes;return payload(b);
    }
    return 0;
}
void *heap_zero(struct heap_pool *h,u32 count,u32 bytes) {
    if(bytes&&count>0xffffffffu/bytes) return 0;
    u32 total=count*bytes;u8 *p=heap_take(h,total);
    if(p) for(u32 i=0;i<total;i++) p[i]=0;
    return p;
}
static void merge(struct heap_block *b) {
    struct heap_block *next=b->next;
    b->size+=(u32)HEADER+next->size;b->next=next->next;
    if(b->next) b->next->prev=b;
    next->magic=0;
}
int heap_release(struct heap_pool *h,void *p) {
    if(!p) return 1;
    if(!h||!h->base||address(p)<address(h->base)+HEADER||address(p)>=address(h->base)+h->bytes||(address(p)&15)) return 0;
    if(!heap_valid(h)) return 0;
    for(struct heap_block *b=h->head;b;b=b->next) if(payload(b)==p) {
        if(b->free) return 0;
        b->free=1;b->requested=0;
        if(b->next&&b->next->free) merge(b);
        if(b->prev&&b->prev->free) merge(b->prev);
        return 1;
    }
    return 0;
}
int heap_inspect(const struct heap_pool *h,struct heap_info *info) {
    if(!info||!heap_valid(h)) return 0;
    info->free_bytes=info->used_bytes=info->allocations=info->largest_free=0;
    for(struct heap_block *b=h->head;b;b=b->next) {
        if(b->free) {info->free_bytes+=b->size;if(b->size>info->largest_free) info->largest_free=b->size;}
        else {info->used_bytes+=b->requested;info->allocations++;}
    }
    return 1;
}
