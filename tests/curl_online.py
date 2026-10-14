#!/usr/bin/env python3
"""Optional guest curl against public HTTP/HTTPS, separate from local peers."""
from harness import Machine
from curl import run, snapshot
with Machine(network=True,logname='curl-online') as machine:
    machine.command('process-trace on','PROCESS: trace on\nnotos> ')
    baseline=snapshot(machine)
    for scheme in ('http','https'):
        reply=run(machine,f'-sS --max-time 15 -o /tmp/public.html {scheme}://example.com/')
        stat=machine.command('stat /tmp/public.html','\nnotos> ')
        assert b'bytes=0 ' not in stat and b'STAT:' in stat,stat
        machine.command('rm /tmp/public.html','FS: file removed\nnotos> ')
        assert snapshot(machine)==baseline
        print('PASS: public '+scheme+' via guest curl, output to file and exact cleanup')
