#!/usr/bin/env python3
"""Actual ring-3 TCP/DNS tests, including loss, lifecycle, OOM limits and blocking."""
import re
import time
from pathlib import Path
from harness import Machine
from socket_fixture import Server,DNS

UNAVAILABLE_CASES = (
    ('sockets-no-nic', {'network': False}),
    ('sockets-no-entropy', {'network': True, 'entropy': False}),
)

def snapshot(m):
    frames=re.search(rb'free_frames=(\d+)',m.command('mem','\nnotos> '))[1]
    heap=re.search(rb'HEAP: [^\n]+\n',m.command('heap','\nnotos> '))[0]
    status=m.command('sockets','\nnotos> ')
    assert b'open=0 queries=0' in status,status
    vfs=m.command('vfs','\nnotos> ')
    assert b'files=0 nodes=0 pages=0 refs=0' in vfs and b'descriptions=0' in vfs,vfs
    return frames,heap

def launch(m,mode,port):
    start=len(m.output);reply=m.command(f'socket-run {mode} {port}','notos> ')
    pid=int(re.search(rb'SOCKET: started pid=(\d+)',reply)[1]);return pid,start

def finish(m,pid,start,mode,status=0):
    m.expect(f'PROCESS: pid={pid} exit=',start,timeout=45)
    line=m.output.index(f'PROCESS: pid={pid} exit='.encode(),start)
    m.expect('\n',line)
    actual=re.search(rb'exit=(\d+)',m.output[line:])[1]
    assert int(actual)==status,bytes(m.output[start:])
    reply=m.command('sockets','\nnotos> ')+bytes(m.output[start:])
    assert b'SOCKET FAIL:' not in reply and b'PROCESS FAULT:' not in reply,reply
    if not status:assert f'SOCKET: {mode} PASS'.encode() in reply,reply
    return reply

def run(m,mode,port):
    pid,start=launch(m,mode,port);return finish(m,pid,start,mode)

for ram in ('16M','64M'):
    with Machine(network=True,memory=ram,logname='sockets-'+ram) as m:
        baseline=snapshot(m)
        for mode,fixture in [('echo','echo'),('nonblock','echo'),('http','http'),('timeout','hold'),('reset','reset')]:
            with Server(fixture) as server:run(m,mode,server.port)
            assert snapshot(m)==baseline
        with Server('echo') as server:
            start=len(m.output);m.p.stdin.write((f'socket-run echo {server.port}\n'*4).encode());m.p.stdin.flush()
            for pid in range(6,10):finish(m,pid,start,'echo')
            assert bytes(m.output[start:]).count(b'SOCKET: echo PASS')==4
        assert snapshot(m)==baseline
        for loss in ('syn','tx','rx'):
            m.command('socket-drop '+loss,'one-packet loss armed\nnotos> ')
            with Server('echo') as server:reply=run(m,'echo',server.port)
            assert b'SOCKET TEST: dropped' in reply,reply
            assert snapshot(m)==baseline
        with Server('echo') as server:run(m,'lifetime',server.port)
        assert snapshot(m)==baseline
        with Server('hold') as server:
            pid,start=launch(m,'hold',server.port);m.expect('SOCKET: receive waiting\n',start)
            reply=m.command('ps','PROCESS: list complete\nnotos> ')
            assert f'PROCESS WAIT: pid={pid} socket=1'.encode() in reply,reply
            m.command('int3','IDT: breakpoint OK; registers and flags preserved\nnotos> ')
            m.command(f'kill {pid}','\nnotos> ');finish(m,pid,start,'hold',143)
        assert snapshot(m)==baseline
        with Server('echo') as server:closed=server.port
        run(m,'refused',closed);assert snapshot(m)==baseline
        run(m,'quota',1);assert snapshot(m)==baseline
        with Server('pool') as server:run(m,'pool',server.port)
        assert snapshot(m)==baseline
    print('sockets: TCP/32KiB/HTTP->tmpfs/nonblocking/timeouts/reset/4 tasks/loss/TIMEWAIT/kill/quota PASS',ram)

with DNS() as dns,Server('echo') as server,Machine(network=True,image='build/notos-socket-test.img',logname='sockets-dns') as m:
    m.command('net-config 10.0.2.15 10.0.2.2 10.0.2.2','configuration applied\nnotos> ')
    baseline=snapshot(m);run(m,'dns',server.port);assert snapshot(m)==baseline
    pid,start=launch(m,'dnswait',server.port);m.expect('SOCKET: resolving slow.socket.test\n',start);time.sleep(.1)
    m.command(f'kill {pid}','\nnotos> ');finish(m,pid,start,'dnswait',143);assert snapshot(m)==baseline
    run(m,'dns',server.port);time.sleep(.7);assert snapshot(m)==baseline
    assert {'echo.socket.test','missing.socket.test','slow.socket.test'}<=set(dns.names),dns.names
print('sockets: DNS A/NXDOMAIN/cancel/stale callback PASS')
for logname,options in UNAVAILABLE_CASES:
    with Machine(logname=logname,**options) as m:
        baseline=snapshot(m);run(m,'unavailable',1);assert snapshot(m)==baseline
assert 'build/sysroot/lib/notos/libc.a' in Path('build/socket-demo.map').read_text()
print('sockets: no NIC/no entropy/upstream static libc PASS')
