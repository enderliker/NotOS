#!/usr/bin/env python3
"""Exercise upstream picolibc in a real guest ELF, never the host C library."""
import re
from pathlib import Path
from harness import Machine

def snapshot(m):
    frames=re.search(rb'free_frames=(\d+)',m.command('mem','\nnotos> '))[1]
    heap=re.search(rb'HEAP: [^\n]+\n',m.command('heap','\nnotos> '))[0]
    return frames,heap

def run(m,pid,mode='test',status=0):
    before=snapshot(m);start=len(m.output)
    m.command('libc-run '+mode,'notos> ')
    m.expect(f'PROCESS: pid={pid} exit={status}',start,timeout=40)
    reply=bytes(m.output[start:])
    assert b'LIBC FAIL:' not in reply and b'PROCESS FAULT:' not in reply,reply
    assert b'LIBC: fini-array PASS\n' in reply,reply
    assert snapshot(m)==before,reply
    return reply

for ram in ('16M','64M'):
    with Machine(memory=ram,logname='libc-'+ram) as m:
        baseline=snapshot(m)
        for pid in (1,2):
            reply=run(m,pid)
            for section in ('strings/format/strtol/qsort/env','malloc/calloc/realloc/free/OOM',
                            'errno/console/time64/calendar/entropy','process-local errno/heap'):
                assert ('LIBC: '+section+' PASS\n').encode() in reply,reply
            assert b'LIBC: main-return PASSLIBC: atexit PASS\n' in reply,reply
        reply=run(m,3,'exit',23)
        assert b'LIBC: buffered outputLIBC: atexit PASS\n' in reply,reply
        # Four private libc data/errno/allocators in identical virtual regions.
        start=len(m.output)
        m.p.stdin.write(b'libc-run test\n'*5);m.p.stdin.flush()
        m.expect('LIBC ERROR: invalid ELF, no slot or memory\nnotos> ',start)
        for pid in range(4,8):m.expect(f'PROCESS: pid={pid} exit=0',start,timeout=45)
        reply=bytes(m.output[start:])
        assert reply.count(b'LIBC: started pid=')==4 and reply.count(b'LIBC: process-local errno/heap PASS\n')==4,reply
        assert b'LIBC FAIL:' not in reply and b'PROCESS FAULT:' not in reply,reply
        assert snapshot(m)==baseline
        m.command('libc-run unknown','LIBC ERROR: choose test/exit/noentropy/files/stdin/disk/hold/fault/curl-file\nnotos> ')
        m.command('user-run bad-buffer','USER: private-CR3/TSS/syscall/cleanup PASS\nnotos> ')
        m.command('elf-test','ELF: load/W^X/BSS/stack/OOM rollback PASS\nnotos> ',timeout=30)
        m.command('heap-test','HEAP: split/coalesce/calloc/OOM selftest PASS\nnotos> ')
        print('PASS',ram,': real libc strings/stdio/allocator/errno/time64/startup/exit, four concurrent processes and complete reclamation')
with Machine(entropy=False,logname='libc-noentropy') as m:
    reply=run(m,1,'noentropy');assert b'FAIL-CLOSED PASS' in reply
    print('PASS: libc getentropy fails closed without a firmware seed')
with Machine(rtc='2050-01-01T00:00:00',logname='libc-time2050') as m:
    run(m,1);print('PASS: libc time/gettimeofday/clock_gettime work beyond signed time32 range')
# Map provenance: actual upstream archive selected by the freestanding linker.
maptext=Path('build/libc-demo.map').read_text()
assert 'sysroot/lib/notos/libc.a' in maptext
assert '/usr/lib/' not in maptext and 'libgcc' not in maptext and 'semihost' not in maptext
