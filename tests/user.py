#!/usr/bin/env python3
import re
from harness import Machine
with Machine(logname='user-ring3') as m:
    assert b'ARCH: TSS selector=0x28; user gate ready\n' in m.output
    registers=m.monitor('info registers')
    assert re.search(rb'TR\s*=0028',registers),registers
    before=m.command('mem','\nnotos> ')
    memory=re.search(rb'free_frames=(\d+)',before)
    for _ in range(3):
        reply=m.command('ring3-test','USER: ring3/TSS/syscall/cleanup PASS\nnotos> ')
        result=re.search(rb'USER: ring=3 ss=35 syscalls=(\d+) exit=42 irq_delta=(\d+)\n',reply)
        assert result and int(result.group(1))>3 and int(result.group(2))>=15,reply
        after=m.command('mem','\nnotos> ')
        assert re.search(rb'free_frames=(\d+)',after).group(1)==memory.group(1),after
        m.command('int3','IDT: breakpoint OK; registers and flags preserved\nnotos> ')
    m.command('vm-test','VM: map/alias/remap/unmap selftest PASS\nnotos> ')
    m.command('heap-test','HEAP: split/coalesce/calloc/OOM selftest PASS\nnotos> ')
    m.command('echo after-ring3','\nafter-ring3\nnotos> ')
    print('PASS: actual CPL3/SS3, TSS stack switch, syscalls, IRQ return, DF and cleanup')
