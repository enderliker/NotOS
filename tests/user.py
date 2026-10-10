#!/usr/bin/env python3
import re
from harness import Machine
PASS='USER: private-CR3/TSS/syscall/cleanup PASS\nnotos> '
with Machine(logname='user-isolation') as m:
    assert b'ARCH: TSS selector=0x28; user gate ready\n' in m.output
    assert re.search(rb'TR\s*=0028',m.monitor('info registers'))
    before=m.command('mem','\nnotos> ')
    memory=re.search(rb'free_frames=(\d+)',before).group(1)
    for _ in range(3):
        reply=m.command('ring3-test',PASS)
        roots=re.findall(rb'USER: cr3=(\d+) expected=(\d+) kernel_restored=1\n',reply)
        assert len(roots)==2 and roots[0][0]!=roots[1][0] and all(a==b for a,b in roots),reply
        assert len(re.findall(rb'USER: ring=3 ss=35 syscalls=\d+ exit=42 irq_delta=\d+\n',reply))==2,reply
        assert re.search(rb'free_frames=(\d+)',m.command('mem','\nnotos> ')).group(1)==memory
    for name,vector,error,cr2 in [
        ('nx-data',14,21,0x08001000),('nx-stack',14,21,0x08003000),
        ('kernel-read',14,5,65536),('code-write',14,7,0x08000000),
        ('io',13,0,0),('guard',14,6,0x08002000),('gate',13,258,0),('foreign-read',14,5,None),
    ]:
        reply=m.command('user-run '+name,PASS)
        fault=re.search(rb'USER FAULT: vector=(\d+) error=(\d+) cr2=(\d+) cpl=3; task terminated\n',reply)
        assert fault and int(fault[1])==vector and int(fault[2])==error,reply
        if cr2 is not None: assert int(fault[3])==cr2,reply
        else: assert 0x100000<=int(fault[3])<0x4000000,reply
        assert b'PANIC:' not in reply
        m.command('echo after-user-fault','\nafter-user-fault\nnotos> ')
        m.command('sleep 5','\nnotos> ')
    reply=m.command('user-run bad-buffer',PASS)
    assert b'exit=42 ' in reply and b'USER OUT:' not in reply and b'PANIC:' not in reply,reply
    reply=m.command('user-run write',PASS)
    assert b'USER OUT: hello from ring3\n' in reply,reply
    reply=m.command('user-run spin',PASS)
    assert b'exit=124 ' in reply and b'USER: budget exceeded; task terminated\n' in reply,reply
    m.command('sleep 20','\nnotos> ')
    m.command('user-run unknown','USER ERROR: unknown scenario\nnotos> ')
    m.command('int3','IDT: breakpoint OK; registers and flags preserved\nnotos> ')
    m.command('vm-test','VM: map/alias/remap/unmap selftest PASS\nnotos> ')
    m.command('heap-test','HEAP: split/coalesce/calloc/OOM selftest PASS\nnotos> ')
    m.command('frame-test','MEM: frame selftest PASS\nnotos> ',timeout=30)
    assert re.search(rb'free_frames=(\d+)',m.command('mem','\nnotos> ')).group(1)==memory
    print('PASS: private CR3/data isolation, TSS, user faults terminated without killing kernel')
    print('PASS: port/gate permissions, validated syscall buffers and IRQ execution budget')
    print('PASS: root/frame restoration and all diagnostic commands after hostile demos')
for memory in ['16M','64M']:
    with Machine(memory=memory,logname='user-'+memory) as m:
        m.command('ring3-test',PASS)
        m.command('user-run kernel-read',PASS)
        m.command('echo recovered','\nrecovered\nnotos> ')
        print(f'PASS: private user roots and fault recovery on {memory} RAM')
