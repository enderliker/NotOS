/* Shell integration and resource-failure probes, separate from the pure parser. */
#include "elf_command.h"
#include "elf.h"
#include "space.h"
#include "task.h"
#include "process.h"
#include "physical.h"
#include "heap.h"
#include <string.h>
#define DECLARE_IMAGE(name) extern const u8 _binary_build_elf_##name##_bin_start[],_binary_build_elf_##name##_bin_end[]
DECLARE_IMAGE(demo);DECLARE_IMAGE(nx_heap);DECLARE_IMAGE(nx_stack);DECLARE_IMAGE(stack_guard);DECLARE_IMAGE(readonly);DECLARE_IMAGE(sleep);
struct bundled_elf {const char *name;const u8 *start,*end;};
#define IMAGE(name,label) {label,_binary_build_elf_##name##_bin_start,_binary_build_elf_##name##_bin_end}
static const struct bundled_elf programs[]={
    IMAGE(demo,"demo"),IMAGE(nx_heap,"nx-heap"),IMAGE(nx_stack,"nx-stack"),
    IMAGE(stack_guard,"stack-guard"),IMAGE(readonly,"readonly"),IMAGE(sleep,"sleep")
};
void elf_command(const char *name) {
    for (unsigned i=0;i<sizeof(programs)/sizeof(programs[0]);i++) {
        if (!equal(name,programs[i].name)) continue;
        u32 pid=process_spawn_elf(name,programs[i].start,(u32)(programs[i].end-programs[i].start));
        if (!pid) console_puts("ELF ERROR: invalid image, no slot or memory\n");
        else {console_puts("ELF: started pid=");console_uint(pid);console_putc('\n');}
        return;
    }
    console_puts("ELF ERROR: choose demo/nx-heap/nx-stack/stack-guard/readonly/sleep\n");
}
static unsigned hold_frames(u32 *held,u32 leave) {
    unsigned count=0;
    while (physical_available()>leave) {
        u32 frame=physical_alloc();
        if (!frame) break;
        held[count++]=frame;
    }
    return count;
}
static int release_frames(u32 *held,unsigned count) {
    int ok=1;
    while (count) if (!physical_free(held[--count])) ok=0;
    return ok;
}
int elf_selftest(void) {
    const u8 *image=programs[0].start;
    u32 bytes=(u32)(programs[0].end-image),before=physical_available();
    struct user_space space={0};
    struct elf_plan plan;
    if (!elf_validate(image,bytes,&plan) || !space_create_elf(&space,image,bytes)) return 0;
    int ok=space.entry==plan.entry && space.stack_top-space.stack_low==65536
        && !space_range(&space,space.stack_low-1,1,1)
        && !space_range(&space,plan.entry,1,1)
        && space_range(&space,space.stack_low,65536,1);
    if (!space_destroy(&space) || physical_available()!=before) return 0;
    for (u32 n=0;n<52;n++) {
        if (space_create_elf(&space,image,n)) {ok=0;space_destroy(&space);}
        if (physical_available()!=before) ok=0;
    }
    u8 *bad=kmalloc(bytes);
    if (!bad) return 0;
    memcpy(bad,image,bytes);bad[4]=2;
    if (space_create_elf(&space,bad,bytes)) {ok=0;space_destroy(&space);}
    if (physical_available()!=before) ok=0;
    kfree(bad);
    u32 *held=kmalloc(before*sizeof(*held));
    if (!held) return 0;
    unsigned count=hold_frames(held,3);
    u32 remaining=physical_available();
    if (space_create_elf(&space,image,bytes)) {ok=0;space_destroy(&space);}
    if (physical_available()!=remaining || space.root) ok=0;
    ok=release_frames(held,count) && ok;
    /* Force rollback after crossing a PAE table boundary, not just on the first page. */
    if (!space_create_elf(&space,image,bytes)) {kfree(held);return 0;}
    count=hold_frames(held,520);
    remaining=physical_available();
    if (space_brk(&space,USER_HEAP_LIMIT)!=USER_HEAP_BASE || space.heap_end!=USER_HEAP_BASE
        || physical_available()!=remaining || space_range(&space,USER_HEAP_BASE,1,0)) ok=0;
    if (space_mmap(&space,USER_ANON_LIMIT-USER_ANON_BASE) || physical_available()!=remaining
        || space_range(&space,USER_ANON_BASE,1,0)) ok=0;
    ok=release_frames(held,count) && ok;
    if (!space_destroy(&space)) ok=0;
    kfree(held);
    void *occupied=kmalloc(1040000);
    if (!occupied) return 0;
    struct task task={0};
    if (task_create_elf(&task,1,image,bytes)) {ok=0;task_destroy(&task);}
    if (physical_available()!=before || task.space.root) ok=0;
    kfree(occupied);
    return ok && physical_available()==before;
}
