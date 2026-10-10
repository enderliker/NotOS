#!/usr/bin/env python3
"""Optional public-internet check through guest DNS/TCP; no guest host socket stubs."""
from harness import Machine
with Machine(network=True,logname='sockets-online') as m:
    start=len(m.output);m.command('socket-run web 80','notos> ')
    m.expect('PROCESS: pid=1 exit=',start,timeout=60)
    line=m.output.index(b'PROCESS: pid=1 exit=',start);m.expect('\n',line)
    assert b'PROCESS: pid=1 exit=0' in m.output[start:] and b'SOCKET: web PASS' in m.output[start:],bytes(m.output[start:])
    status=m.command('sockets','\nnotos> ')
    assert b'open=0 queries=0' in status,status
    assert b'SOCKET FAIL:' not in m.output and b'PROCESS FAULT:' not in m.output
print('sockets: real userspace DNS + public HTTP 200 PASS')
