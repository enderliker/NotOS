#include "paging.h"
#include "paging_entries.h"
#include "physical.h"
static struct page_entry root[4] __attribute__((aligned(4096)));
static struct page_entry directories[4][512] __attribute__((aligned(4096)));
static struct page_entry identity[PHYSICAL_LIMIT/(4096*512)][512] __attribute__((aligned(4096)));
static struct page_entry window[2][512] __attribute__((aligned(4096)));
extern char __readonly_start[],__readonly_end[],__text_end[],__kernel_static_start[],__bss_start[],__bss_end[];
static u32 control(void) {u32 v;__asm__ volatile("mov %%cr0,%0":"=r"(v));return v;}
static void invalidate(u32 address) {__asm__ volatile("invlpg (%0)"::"r"(address):"memory");}
static int cpu_nx(void) {
    u32 original,changed;
    __asm__ volatile("pushfl;popl %0":"=r"(original));
    u32 attempt=original^0x200000u;
    __asm__ volatile("pushl %1;popfl;pushfl;popl %0;pushl %2;popfl":"=&r"(changed):"r"(attempt),"r"(original):"memory","cc");
    if (!((changed^original)&0x200000u)) return 0;
    u32 a,b,c,d;
    __asm__ volatile("cpuid":"=a"(a),"=b"(b),"=c"(c),"=d"(d):"a"(1));
    if ((d&96)!=96) return 0; /* PAE and MSR instructions. */
    __asm__ volatile("cpuid":"=a"(a),"=b"(b),"=c"(c),"=d"(d):"a"(0x80000000u));
    if (a<0x80000001u) return 0;
    __asm__ volatile("cpuid":"=a"(a),"=b"(b),"=c"(c),"=d"(d):"a"(0x80000001u));
    return (d&0x100000u)!=0;
}
int paging_init(void) {
    if (!cpu_nx()) return 0;
    for (unsigned i=0;i<4;i++) {root[i]=(struct page_entry){(u32)directories[i]|1u,0};
        for (unsigned j=0;j<512;j++) directories[i][j]=(struct page_entry){0,0};}
    for (unsigned table=0;table<PHYSICAL_LIMIT/(4096*512);table++) {
        directories[0][table]=(struct page_entry){(u32)identity[table]|3u,0};
        for (unsigned i=0;i<512;i++) {
            u32 address=(table*512+i)*4096,flags=3;
            int kernel_static=address>=(u32)__kernel_static_start && address<(u32)__bss_end;
            int execute=address>=(u32)__readonly_start && address<(u32)__text_end;
            if (!address || address==0x90000 || (address>=0x100000 && !kernel_static && !physical_usable(address))) flags=0;
            else if (address>=(u32)__readonly_start && address<(u32)__readonly_end) flags=1;
            else if (address>=(u32)__kernel_static_start && address<(u32)__bss_start) flags=1;
            else if (address>=0xa0000 && address<0xc0000) flags|=0x18;
            if (flags) page_entry_set(&identity[table][i],address,flags,execute);
            else identity[table][i]=(struct page_entry){0,0};
        }
    }
    for (unsigned n=0;n<2;n++) {
        directories[3][((VM_BASE>>21)&511)+n]=(struct page_entry){(u32)window[n]|3u,0};
        for (unsigned i=0;i<512;i++) window[n][i]=(struct page_entry){0,0};
    }
    u32 low,high,cr4;
    __asm__ volatile("rdmsr":"=a"(low),"=d"(high):"c"(0xc0000080u));
    low|=0x800u;__asm__ volatile("wrmsr"::"a"(low),"d"(high),"c"(0xc0000080u):"memory");
    __asm__ volatile("mov %%cr4,%0":"=r"(cr4));cr4|=0x20u;
    __asm__ volatile("mov %0,%%cr4"::"r"(cr4):"memory");
    __asm__ volatile("mov %0,%%cr3"::"r"((u32)root):"memory");
    u32 cr0=control()|0x80010000u;__asm__ volatile("mov %0,%%cr0"::"r"(cr0):"memory");
    return (control()&0x80010000u)==0x80010000u;
}
static int in_window(u32 address) {return address>=VM_BASE && address<VM_BASE+VM_SIZE && !(address&4095);}
static struct page_entry *window_entry(u32 address) {unsigned index=(address-VM_BASE)/4096;return &window[index/512][index%512];}
int paging_map(u32 virtual_address,u32 physical,int writable) {
    if (!in_window(virtual_address)) return 0;
    u32 flags=irq_save();struct page_entry *entry=window_entry(virtual_address);
    if ((entry->low&1) || !physical_owned(physical)) {irq_restore(flags);return 0;}
    page_entry_set(entry,physical,1u|(writable?2u:0u),0);invalidate(virtual_address);irq_restore(flags);return 1;
}
u32 paging_unmap(u32 address) {
    if (!in_window(address)) return 0;
    u32 flags=irq_save();struct page_entry *entry=window_entry(address);
    if (!(entry->low&1)) {irq_restore(flags);return 0;}
    u32 physical=entry->low&0xfffff000u;
    page_entry_clear(entry);invalidate(address);irq_restore(flags);return physical;
}
u32 paging_current_root(void) {u32 value;__asm__ volatile("mov %%cr3,%0":"=r"(value));return value;}
u32 paging_translate(u32 address) {
    u32 flags=irq_save();const struct page_entry *pdpt=(const struct page_entry *)paging_current_root();
    struct page_entry p=pdpt[address>>30],entry={0,0};
    if (p.low&1) {
        const struct page_entry *directory=(const struct page_entry *)(p.low&0xfffff000u);p=directory[(address>>21)&511];
        if (p.low&1) {const struct page_entry *table=(const struct page_entry *)(p.low&0xfffff000u);entry=table[(address>>12)&511];}
    }
    u32 physical=(entry.low&1)?(entry.low&0xfffff000u)|(address&4095):0;irq_restore(flags);return physical;
}
int paging_copy_kernel_directory(u32 physical,u32 private_directory) {
    if (!physical_owned(physical) || !physical_owned(private_directory)) return 0;
    struct page_entry *pdpt=(struct page_entry *)physical,*directory=(struct page_entry *)private_directory;
    for (unsigned i=0;i<4;i++) pdpt[i]=root[i];
    for (unsigned i=0;i<512;i++) directory[i]=directories[0][i];
    pdpt[0]=(struct page_entry){private_directory|1u,0};return 1;
}
void paging_status(void) {console_puts("VM: paging=1 wp=1 page_bytes=4096\nVM: PAE=1 NX=1 kernel W^X\n");}
int paging_selftest(void) {
    const u32 va=VM_BASE+VM_SIZE-8192,vb=VM_BASE+VM_SIZE-4096;
    if (paging_translate(va)||paging_translate(vb)) return 0;
    unsigned before=physical_available();u32 a=physical_alloc(),b=physical_alloc();int ok=1;
    if (!a||!b) {if(a)physical_free(a);if(b)physical_free(b);return 0;}
    if (paging_map(va+1,a,1)||paging_map(0,a,1)||paging_map(va,0,1)) ok=0;
    if (!paging_map(va,a,1)||!paging_map(vb,a,1)) ok=0;
    if (ok) {
        *(volatile u32 *)va=0x12345678;
        if (*(volatile u32 *)vb!=0x12345678 || *(volatile u32 *)a!=0x12345678 || paging_translate(va+17)!=a+17) ok=0;
        if (paging_map(va,b,1)) ok=0;
        if (paging_unmap(va)!=a || !paging_map(va,b,1)) ok=0;
        if (ok) {*(volatile u32 *)va=0x55667788;if(*(volatile u32 *)b!=0x55667788 || *(volatile u32 *)vb!=0x12345678)ok=0;}
    }
    paging_unmap(va);paging_unmap(vb);
    if (paging_translate(va)||paging_translate(vb)||!physical_free(a)||!physical_free(b)||physical_available()!=before) ok=0;
    return ok;
}
void paging_fault_null(void) {__asm__ volatile("movl 0,%%eax":::"eax","memory");}
void paging_fault_readonly(void) {__asm__ volatile("movb $0,(%0)"::"r"(__readonly_start):"memory");}
void paging_fault_guard(void) {__asm__ volatile("movl (%0),%%eax"::"r"(VM_BASE):"eax","memory");}
void paging_fault_unmapped(void) {
    u32 address=VM_BASE+VM_SIZE-4096,physical=physical_alloc();
    if (!physical||!paging_map(address,physical,1)) {if(physical)physical_free(physical);return;}
    *(volatile u32 *)address=0xabcdef01;paging_unmap(address);physical_free(physical);
    __asm__ volatile("movl (%0),%%eax"::"r"(address):"eax","memory");
}
