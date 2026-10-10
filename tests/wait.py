#!/usr/bin/env python3
"""Managed ELF sleeps release the CPU and resume their saved syscall frames."""
import re
from harness import Machine

def snapshot(m):
    frames=re.search(rb'free_frames=(\d+)',m.command('mem','\nnotos> '))[1]
    heap=re.search(rb'HEAP: [^\n]+\n',m.command('heap','\nnotos> '))[0]
    return frames,heap

with Machine(logname='blocked-wait',network=True) as m:
    baseline=snapshot(m)
    start=len(m.output)
    m.command('elf-run sleep','USER OUT: ELF sleep begin\n')
    reply=m.command('ps','PROCESS: list complete\nnotos> ')
    assert b'PROCESS WAIT: pid=1 timeout_ms=600\n' in reply,reply
    assert b'ELF sleep woke' not in m.output[start:]
    m.command('echo responsive','\nresponsive\nnotos> ')
    m.command('int3','IDT: breakpoint OK; registers and flags preserved\nnotos> ')
    m.command('net','NET: idle\nnotos> ')
    m.command('spawn worker','PROCESS: started pid=2')
    m.expect('PROCESS: pid=2 exit=42',start)
    assert b'PROCESS: pid=1 exit=' not in m.output[start:]
    m.expect('USER OUT: ELF sleep woke\n',start)
    m.expect('PROCESS: pid=1 exit=0',start)
    assert snapshot(m)==baseline
    reply=bytes(m.output[start:])
    ticks=int(re.search(rb'PROCESS: pid=1 exit=0 ticks=(\d+)',reply)[1])
    assert ticks<10,reply
    # Cancellation frees a blocked task and cannot awaken its reused slot.
    start=len(m.output)
    m.command('elf-run sleep','USER OUT: ELF sleep begin\n')
    reply=m.command('ps','PROCESS: list complete\nnotos> ')
    assert b'PROCESS WAIT: pid=3 timeout_ms=600\n' in reply,reply
    reply=m.command('kill 3','\nnotos> ')
    assert b'PROCESS: pid=3 exit=143' in reply,reply
    assert snapshot(m)==baseline
    m.command('sleep 650','\nnotos> ')
    assert b'ELF sleep woke' not in m.output[start:]
    # All slots can be blocked at once while shell, clock and devices keep running.
    start=len(m.output)
    m.p.stdin.write(b'elf-run sleep\n'*5);m.p.stdin.flush()
    m.expect('ELF ERROR: invalid image, no slot or memory\nnotos> ',start)
    for _ in range(20):
        # Allow each saved startup frame to yield once, then register its wait.
        reply=m.command('ps','PROCESS: list complete\nnotos> ')
        if reply.count(b'PROCESS WAIT:')==4:break
    assert reply.count(b'PROCESS WAIT:')==4,reply
    m.command('echo all-blocked-responsive','\nall-blocked-responsive\nnotos> ')
    for pid in range(4,8):m.expect(f'PROCESS: pid={pid} exit=0',start,timeout=10)
    assert snapshot(m)==baseline
    assert m.output[start:].count(b'ELF sleep woke\n')==4
    m.command('user-run bad-buffer','USER: private-CR3/TSS/syscall/cleanup PASS\nnotos> ')
    m.command('heap-test','HEAP: split/coalesce/calloc/OOM selftest PASS\nnotos> ')
    assert b'PANIC:' not in m.output and b'SCHED PANIC:' not in m.output
    print('PASS: blocked syscall resume, no CPU-budget consumption, concurrent worker, cancellation, slot reuse and all-blocked responsiveness')
