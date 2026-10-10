#!/usr/bin/env python3
"""Optional real internet test; requires outbound DNS and plain HTTP."""
import re
from harness import Machine
with Machine(network=True,logname="online") as m:
    reply=m.command('dns example.com', 'DNS: example.com -> ',timeout=15)
    # Wait for a subsequent prompt/status command to collect the complete line.
    reply+=m.command('net', 'NET: idle\n')
    assert re.search(rb'DNS: example\.com -> (?:\d{1,3}\.){3}\d{1,3}\n',reply), reply
    m.command('user-run bad-buffer','USER: private-CR3/TSS/syscall/cleanup PASS\nnotos> ')
    m.command('multitask-run four','SCHED: preemptive multitask/contexts/cleanup PASS\nnotos> ')
    m.command('multitask-run fpu','SCHED: preemptive multitask/contexts/cleanup PASS\nnotos> ')
    reply=m.command('http example.com', ' bytes\n',timeout=25)
    assert re.search(rb'HTTP: HTTP/1\.[01] \d{3}', reply), reply
    print('PASS: external DNS lookup and real internet HTTP response')
    print(reply.decode(errors='replace'))
