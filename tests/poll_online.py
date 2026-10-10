#!/usr/bin/env python3
"""Optional DNS + nonblocking HTTP using guest poll() on a public peer."""
from harness import Machine
with Machine(network=True,logname='poll-online') as m:
    start=len(m.output);m.command('socket-run poll-web 80','notos> ')
    m.expect('PROCESS: pid=1 exit=',start,timeout=60)
    line=m.output.index(b'PROCESS: pid=1 exit=',start);m.expect('\n',line)
    assert b'PROCESS: pid=1 exit=0' in m.output[start:] and b'POLL: poll-web PASS' in m.output[start:],bytes(m.output[start:])
    assert b'POLL FAIL:' not in m.output[start:] and b'PROCESS FAULT:' not in m.output[start:]
    status=m.command('sockets','\nnotos> ');assert b'open=0 queries=0' in status,status
print('PASS: guest poll + nonblocking DNS/TCP/public HTTP 200')
