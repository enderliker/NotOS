#include "paging.h"
#include "physical.h"
#include "user.h"
static u32 directory[1024] __attribute__((aligned(4096)));
static u32 identity[PHYSICAL_LIMIT/(4096*1024)][1024] __attribute__((aligned(4096)));
static u32 window[1024] __attribute__((aligned(4096)));
static u32 user_table[1024] __attribute__((aligned(4096)));
extern char __readonly_start[],__readonly_end[];
static u32 control(void) {u32 v;__asm__ volatile("mov %%cr0,%0" : "=r"(v));return v;}
static void invalidate(u32 address) {__asm__ volatile("invlpg (%0)" :: "r"(address) : "memory");}
int paging_init(void) {
    for(unsigned i=0;i<1024;i++) directory[i]=window[i]=user_table[i]=0;
    for(unsigned table=0;table<PHYSICAL_LIMIT/(4096*1024);table++) {
        directory[table]=(u32)identity[table]|3;
        for(unsigned i=0;i<1024;i++) {
            u32 address=(table*1024+i)*4096;
            u32 flags=3;
            if(!address || address==0x80000 || (address>=0x100000 && !physical_usable(address))) flags=0;
            else if(address>=(u32)__readonly_start && address<(u32)__readonly_end) flags=1;
            else if(address>=0xa0000 && address<0xc0000) flags|=0x18; /* VGA uncached. */
            identity[table][i]=flags?(address|flags):0;
        }
    }
    directory[VM_BASE>>22]=(u32)window|3;
    directory[USER_BASE>>22]=(u32)user_table|7;
    __asm__ volatile("mov %0,%%cr3" :: "r"((u32)directory) : "memory");
    u32 cr0=control()|0x80010000u; /* PG + WP: supervisor writes honor read-only PTEs. */
    __asm__ volatile("mov %0,%%cr0" :: "r"(cr0) : "memory");
    return (control()&0x80010000u)==0x80010000u;
}
static int in_window(u32 address) {
    return address>=VM_BASE && address<VM_BASE+VM_SIZE && !(address&4095);
}
int paging_map(u32 v,u32 physical,int writable) {
    if(!in_window(v)) return 0;
    u32 flags=irq_save();unsigned index=(v-VM_BASE)/4096;
    if(window[index]&1 || !physical_owned(physical)) {irq_restore(flags);return 0;}
    window[index]=physical|1u|(writable?2u:0u);invalidate(v);
    irq_restore(flags);return 1;
}
u32 paging_unmap(u32 v) {
    if(!in_window(v)) return 0;
    u32 flags=irq_save();unsigned index=(v-VM_BASE)/4096;
    u32 physical=window[index]&0xfffff000u;
    window[index]=0;invalidate(v);irq_restore(flags);return physical;
}
u32 paging_translate(u32 address) {
    u32 flags=irq_save(),entry=0;
    if(address<PHYSICAL_LIMIT) entry=identity[address>>22][(address>>12)&1023];
    else if(address>=USER_BASE && address<USER_BASE+USER_SIZE) entry=user_table[(address-USER_BASE)/4096];
    else if(address>=VM_BASE && address<VM_BASE+VM_SIZE) entry=window[(address-VM_BASE)/4096];
    u32 physical=(entry&1)?((entry&0xfffff000u)|(address&4095)):0;
    irq_restore(flags);return physical;
}
int paging_user_map(u32 v,u32 physical,int writable) {
    if(v<USER_BASE||v>=USER_BASE+USER_SIZE||(v&4095)) return 0;
    u32 flags=irq_save();unsigned index=(v-USER_BASE)/4096;
    if(user_table[index]&1||!physical_owned(physical)) {irq_restore(flags);return 0;}
    user_table[index]=physical|5u|(writable?2u:0u);invalidate(v);irq_restore(flags);return 1;
}
u32 paging_user_unmap(u32 v) {
    if(v<USER_BASE||v>=USER_BASE+USER_SIZE||(v&4095)) return 0;
    u32 flags=irq_save();unsigned index=(v-USER_BASE)/4096;
    u32 physical=user_table[index]&0xfffff000u;user_table[index]=0;
    invalidate(v);irq_restore(flags);return physical;
}
void paging_status(void) {
    u32 cr0=control();
    console_puts("VM: paging=");console_uint((cr0>>31)&1);
    console_puts(" wp=");console_uint((cr0>>16)&1);
    console_puts(" page_bytes=4096\n");
}
int paging_selftest(void) {
    const u32 va=VM_BASE+VM_SIZE-8192,vb=VM_BASE+VM_SIZE-4096;
    if(paging_translate(va)||paging_translate(vb)) return 0;
    unsigned before=physical_available();u32 a=physical_alloc(),b=physical_alloc();int ok=1;
    if(!a||!b) {if(a) physical_free(a);if(b) physical_free(b);return 0;}
    if(paging_map(va+1,a,1)||paging_map(0,a,1)||paging_map(va,0,1)) ok=0;
    if(!paging_map(va,a,1)||!paging_map(vb,a,1)) ok=0;
    if(ok) {
        *(volatile u32 *)va=0x12345678;
        if(*(volatile u32 *)vb!=0x12345678 || *(volatile u32 *)a!=0x12345678 || paging_translate(va+17)!=a+17) ok=0;
        if(paging_map(va,b,1)) ok=0;
        if(paging_unmap(va)!=a||!paging_map(va,b,1)) ok=0;
        if(ok) {
            *(volatile u32 *)va=0x55667788;
            if(*(volatile u32 *)b!=0x55667788 || *(volatile u32 *)vb!=0x12345678) ok=0;
        }
    }
    (void)paging_unmap(va);(void)paging_unmap(vb);
    if(paging_translate(va)||paging_translate(vb)||paging_unmap(va)) ok=0;
    if(!physical_free(a)||!physical_free(b)||physical_available()!=before) ok=0;
    return ok;
}
void paging_fault_null(void) {__asm__ volatile("movl 0,%%eax" ::: "eax","memory");}
void paging_fault_readonly(void) {__asm__ volatile("movb $0,(%0)" :: "r"(__readonly_start) : "memory");}
void paging_fault_guard(void) {__asm__ volatile("movl (%0),%%eax" :: "r"(VM_BASE) : "eax","memory");}
void paging_fault_unmapped(void) {
    u32 address=VM_BASE+VM_SIZE-4096,physical=physical_alloc();
    if(!physical||!paging_map(address,physical,1)) {if(physical) physical_free(physical);console_puts("VM ERROR: fault setup failed\n");return;}
    *(volatile u32 *)address=0xabcdef01; /* Populate TLB before invalidation. */
    paging_unmap(address);physical_free(physical);
    __asm__ volatile("movl (%0),%%eax" :: "r"(address) : "eax","memory");
}
