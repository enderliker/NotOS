#!/usr/bin/env python3
import re
from harness import Machine
for memory in ['16M','32M','64M','128M']:
    with Machine(memory=memory,logname='physical-'+memory) as m:
        assert b'BOOT: 64 KiB loader verified\n' in m.output
        reply=m.command('mem','\nnotos> ')
        match=re.search(rb'MEM: E820 entries=(\d+) managed_frames=(\d+) free_frames=(\d+) cap_MiB=64\n',reply)
        assert match,reply
        entries,total,free=map(int,match.groups())
        limit=min(int(memory[:-1]),64)*1024*1024//4096
        assert 0<entries<=64 and limit-512<=total<=limit-256 and free==total,reply
        m.command('frame-test','MEM: frame selftest PASS\nnotos> ',timeout=30)
        after=m.command('mem','\nnotos> ')
        assert match.group(0) in after,after
        m.command('echo after-frame-test','\nafter-frame-test\nnotos> ')
        print(f'PASS: E820 + frame allocation/exhaustion/release on {memory} RAM')
