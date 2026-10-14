#include "arch.h"
#include "fs.h"
#include "pic.h"
#include "process_abi.h"
#include "syscall_numbers.h"
struct descriptor {u16 limit,base_low;u8 base_mid,access,granularity,base_high;} __attribute__((packed));
struct pointer {u16 limit;u32 base;} __attribute__((packed));
struct tss32 {
    u32 previous,esp0,ss0,esp1,ss1,esp2,ss2,cr3,eip,eflags;
    u32 eax,ecx,edx,ebx,esp,ebp,esi,edi,es,cs,ss,ds,fs,gs,ldt;
    u16 trap,iomap;
} __attribute__((packed));
static struct descriptor gdt[6] __attribute__((aligned(16)));
static struct tss32 tss;
static u8 privilege_stack[16384] __attribute__((aligned(4096)));
static u32 selected_stack_low, selected_stack_top;
extern void gdt_load(const struct pointer *pointer);
_Static_assert(sizeof(struct tss32)==104,"32-bit TSS size");
_Static_assert(sizeof(struct descriptor)==8,"GDT entry size");
static void entry(unsigned n,u32 base,u32 limit,u8 access,u8 granularity) {
    gdt[n].limit=(u16)limit;gdt[n].base_low=(u16)base;
    gdt[n].base_mid=(u8)(base>>16);gdt[n].access=access;
    gdt[n].granularity=(u8)((limit>>16)&15)|granularity;gdt[n].base_high=(u8)(base>>24);
}
u32 arch_kernel_stack_low(void) { return selected_stack_low; }
u32 arch_kernel_stack_top(void) { return selected_stack_top; }
void arch_select_kernel_stack(u32 low, u32 top) {
    selected_stack_low = low;
    selected_stack_top = top;
    tss.esp0 = top;
}
void arch_reset_kernel_stack(void) {
    arch_select_kernel_stack((u32)privilege_stack, (u32)(privilege_stack + sizeof(privilege_stack)));
}
void arch_init(void) {
    entry(0,0,0,0,0);entry(1,0,0xfffff,0x9a,0xc0);entry(2,0,0xfffff,0x92,0xc0);
    entry(3,0,0xfffff,0xfa,0xc0);entry(4,0,0xfffff,0xf2,0xc0);
    arch_reset_kernel_stack();tss.ss0=0x10;tss.iomap=sizeof(tss);
    entry(5,(u32)&tss,sizeof(tss)-1,0x89,0);
    const struct pointer p={sizeof(gdt)-1,(u32)gdt};gdt_load(&p);
    __asm__ volatile("ltr %%ax" :: "a"((u16)0x28) : "memory");
}
static void serial_drain(void) {
    for (unsigned i = 0; i < 65536; i++)
        if ((inb(0x3fd) & 0x60) == 0x60) break;
}
int arch_power(u32 action) {
    if (action != NOTOS_POWER_REBOOT && action != NOTOS_POWER_POWEROFF && action != NOTOS_POWER_HALT)
        return -ERROR_INVALID_ARGUMENT;
    fs_sync();
    __asm__ volatile("cli" ::: "memory");
    for (unsigned irq = 0; irq < 16; irq++) pic_mask(irq);
    if (action == NOTOS_POWER_REBOOT) {
        console_puts("POWER: rebooting\n");
        serial_drain();
        for (unsigned i = 0; i < 65536; i++)
            if (!(inb(0x64) & 2)) break;
        outb(0x64, 0xfe);
        for (unsigned i = 0; i < 65536; i++) __asm__ volatile("pause");
        /* Deliberate triple-fault fallback if 8042 reset line is unavailable. */
        const struct pointer zero_idt = {0, 0};
        __asm__ volatile("lidt (%0); int3" :: "r"(&zero_idt) : "memory");
    }
    if (action == NOTOS_POWER_POWEROFF) {
        console_puts("POWER: powering off\n");
        serial_drain();
        /* QEMU PIIX4 and Bochs ACPI PM1a_CNT_BLK shutdown ports. */
        outw(0x604, 0x2000);
        outw(0xb004, 0x2000);
    }
    console_puts("POWER: system halted\n");
    serial_drain();
    for (;;) __asm__ volatile("cli; hlt");
}
