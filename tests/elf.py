#!/usr/bin/env python3
"""Execute actual static ELF32 and per-process virtual memory at CPL3."""
import re
from harness import Machine

def free_frames(machine):
    return re.search(rb'free_frames=(\d+)',machine.command('mem','\nnotos> '))[1]

for ram in ('16M','64M'):
    with Machine(memory=ram,logname='elf-'+ram) as m:
        before=free_frames(m)
        for _ in range(2):
            m.command('elf-test','ELF: load/W^X/BSS/stack/OOM rollback PASS\nnotos> ',timeout=30)
        pid=1
        for _ in range(3):
            reply=m.command('elf-run demo',f'PROCESS: pid={pid} exit=0',timeout=30)
            assert b'USER OUT: SPANUSER OUT: ELF32 BSS/stack/brk/mmap PASS\n' in reply,reply
            pid+=1
            assert free_frames(m)==before
        for name,error,address in [('nx-heap',21,0x08400000),('nx-stack',21,None),
                                   ('stack-guard',6,0x08feffff),('readonly',7,0x08000000)]:
            start=len(m.output)
            m.command('elf-run '+name,'ELF: started pid=')
            m.expect('PROCESS FAULT:',start,timeout=30)
            m.command('echo recovered','\nrecovered\nnotos> ')
            reply=bytes(m.output[start:])
            assert f'PROCESS: pid={pid} exit=142'.encode() in reply,reply
            pid+=1
            fault=re.search(rb'PROCESS FAULT: vector=(\d+) error=(\d+) cr2=(\d+)\n',reply)
            assert fault and int(fault[1])==14 and int(fault[2])==error,reply
            if address is not None:assert int(fault[3])==address,reply
            else:assert 0x08ff0000<=int(fault[3])<0x09000000,reply
            assert free_frames(m)==before
        # All commands fit the serial event-loop batch: four live slots before execution.
        start=len(m.output)
        m.p.stdin.write(b'elf-run demo\n'*5);m.p.stdin.flush()
        m.expect('ELF ERROR: invalid image, no slot or memory\n',start)
        for target in range(pid,pid+4):
            m.expect(f'PROCESS: pid={target} exit=0',start,timeout=30)
        reply=bytes(m.output[start:])
        assert reply.count(b'ELF: started pid=')==4,reply
        assert reply.count(b'ELF32 BSS/stack/brk/mmap PASS\n')==4,reply
        pid+=4
        assert free_frames(m)==before
        m.command('elf-run missing','ELF ERROR: choose demo/nx-heap/nx-stack/stack-guard/readonly/sleep\nnotos> ')
        m.command('ps','PROCESS: list complete\nnotos> ')
        m.command('ring3-test','USER: private-CR3/TSS/syscall/cleanup PASS\nnotos> ')
        m.command('run-user repeat 2 { print 7 }',f'PROCESS: pid={pid} exit=0')
        m.command('heap-test','HEAP: split/coalesce/calloc/OOM selftest PASS\nnotos> ')
        assert b'PANIC:' not in m.output
        print('PASS',ram,': actual ELF CPL3, BSS, 64KiB stack, brk/mmap across PAE tables, concurrent isolation and OOM rollback')
