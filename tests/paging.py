#!/usr/bin/env python3
import re
import time
from harness import Machine
with Machine(logname='paging') as m:
    assert b'VM: paging=1 wp=1 page_bytes=4096\n' in m.output
    registers=m.monitor('info registers')
    assert re.search(rb'CR0=800100[0-9a-f]{2}',registers),registers
    assert int(re.search(rb'CR4=([0-9a-f]+)',registers)[1],16)&32,registers
    assert int(re.search(rb'EFER=([0-9a-f]+)',registers)[1],16)&0x800,registers
    assert b'VM: PAE=1 NX=1 kernel W^X' in m.output
    for _ in range(3):
        m.command('vm-test','VM: map/alias/remap/unmap selftest PASS\nnotos> ')
    m.command('sleep 20','\nnotos> ')
    m.command('run print 9 * 9','\n81\nnotos> ')
    print('PASS: real CR0.PG/WP, high virtual aliases, remapping, TLB and frame restoration')
for fault,error,cr2 in [
    ('null','00000000','00000000'),
    ('readonly','00000003','00010000'),
    ('guard','00000000','d0000000'),
    ('unmapped','00000000','d03ff000'),
]:
    with Machine(logname='pagefault-'+fault) as m:
        reply=m.command('fault '+fault,'SYSTEM HALTED: restart required\n')
        assert b'PANIC: vector=14 Page Fault\n' in reply,reply
        assert ('ERROR=0x'+error).encode() in reply,reply
        assert ('CR2=0x'+cr2+'\n').encode() in reply,reply
        deadline=time.monotonic()+2
        while b'HLT=1' not in m.monitor('info registers'):
            assert time.monotonic()<deadline,'CPU did not halt after panic'
            time.sleep(.01)
        assert m.p.poll() is None,'CPU reset/triple fault'
        m.screenshot('pagefault-'+fault)
        print(f'PASS: actual #PF {fault}, correct error/CR2 and halted diagnostic screen')
