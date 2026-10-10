#!/usr/bin/env python3
import re
import time
from harness import Machine
with Machine(logname='heap') as m:
    reply=m.command('heap','\nnotos> ')
    baseline=re.search(rb'HEAP: bytes=1048576 free_bytes=(\d+) used_bytes=0 allocations=0 largest_free=(\d+)\n',reply)
    assert baseline and baseline.group(1)==baseline.group(2),reply
    for _ in range(3):
        m.command('heap-test','HEAP: split/coalesce/calloc/OOM selftest PASS\nnotos> ')
    sizes=[1,16,33,4097,65535,81,1024,8192]
    for slot,size in enumerate(sizes):
        m.command('alloc '+str(size),f'HEAP: slot={slot} bytes={size}\nnotos> ')
    m.command('alloc 1','HEAP ERROR: all slots occupied\nnotos> ')
    reply=m.command('heap','\nnotos> ')
    assert f'used_bytes={sum(sizes)} allocations=8'.encode() in reply,reply
    for slot in [3,0,7,2,5,1,6,4]: m.command('free '+str(slot),f'HEAP: freed slot={slot}\nnotos> ')
    after=m.command('heap','\nnotos> ')
    assert baseline.group(0) in after,after
    for bad,error in [('free 0','invalid or empty slot'),('free 8','invalid or empty slot'),('free -1','invalid number'),('alloc 4294967296','invalid number'),('alloc x','invalid number')]:
        m.command(bad,'HEAP ERROR: '+error+'\nnotos> ')
    m.command('alloc 0','HEAP ERROR: allocation failed\nnotos> ')
    m.command('alloc 4294967295','HEAP ERROR: allocation failed\nnotos> ')
    m.command('vm-test','VM: map/alias/remap/unmap selftest PASS\nnotos> ')
    m.command('frame-test','MEM: frame selftest PASS\nnotos> ',timeout=30)
    m.command('heap-test','HEAP: split/coalesce/calloc/OOM selftest PASS\nnotos> ')
    m.command('run print 40 + 2','\n42\nnotos> ')
    print('PASS: mapped heap, real allocations, reuse, coalescing and error recovery inside guest')
with Machine(logname='heap-guard') as m:
    reply=m.command('fault heap-end','SYSTEM HALTED: restart required\n')
    assert b'PANIC: vector=14 Page Fault\n' in reply,reply
    assert b'ERROR=0x00000002' in reply and b'CR2=0xd0101000\n' in reply,reply
    deadline=time.monotonic()+2
    while b'HLT=1' not in m.monitor('info registers'):
        assert time.monotonic()<deadline
        time.sleep(.01)
    m.screenshot('heap-guard')
    print('PASS: write beyond heap reaches an unmapped guard and raises actual #PF')
