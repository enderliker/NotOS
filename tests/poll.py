#!/usr/bin/env python3
"""Real guest multi-FD waits: no userspace busy polling or host syscall proxies."""
import re
import socketserver
import threading
import time
from harness import Machine
from socket_fixture import Server

class PollPeer(socketserver.ThreadingTCPServer):
    allow_reuse_address=True
    daemon_threads=True
    def __init__(self,mode='delayed'):
        self.mode=mode;self.errors=[]
        super().__init__(('127.0.0.1',0),PollHandler)
        self.thread=threading.Thread(target=self.serve_forever,daemon=True);self.thread.start()
    @property
    def port(self):return self.server_address[1]
    def __enter__(self):return self
    def __exit__(self,*args):
        self.shutdown();self.server_close();self.thread.join();assert not self.errors,self.errors
class PollHandler(socketserver.BaseRequestHandler):
    def handle(self):
        self.request.settimeout(15)
        try:
            if self.server.mode=='backpressure':
                time.sleep(.5)
                while self.request.recv(4096):pass
            else:
                byte=self.request.recv(1);assert byte in (b'a',b'b'),byte
                time.sleep(.4 if byte==b'a' else .8);self.request.sendall(byte)
                while self.request.recv(32):pass
        except Exception as error:self.server.errors.append(repr(error))

def snapshot(m):
    frames=re.search(rb'free_frames=(\d+)',m.command('mem','\nnotos> '))[1]
    heap=re.search(rb'HEAP: [^\n]+\n',m.command('heap','\nnotos> '))[0]
    status=m.command('sockets','\nnotos> ');assert b'open=0 queries=0' in status,status
    status=m.command('vfs','\nnotos> ')
    assert b'files=0 nodes=0 pages=0 refs=0' in status and b'descriptions=0' in status,status
    return frames,heap

def launch(m,mode,port=1):
    start=len(m.output);reply=m.command(f'socket-run {mode} {port}','notos> ')
    return int(re.search(rb'SOCKET: started pid=(\d+)',reply)[1]),start

def finish(m,pid,start,mode,status=0):
    m.expect(f'PROCESS: pid={pid} exit=',start,timeout=15)
    line=m.output.index(f'PROCESS: pid={pid} exit='.encode(),start);m.expect('\n',line)
    actual=int(re.search(rb'exit=(\d+)',m.output[line:])[1]);assert actual==status,bytes(m.output[start:])
    if not status:assert f'POLL: {mode} PASS'.encode() in m.output[start:],bytes(m.output[start:])
    assert b'POLL FAIL:' not in m.output[start:],bytes(m.output[start:])
    return bytes(m.output[start:])

def run(m,mode,port=1):
    pid,start=launch(m,mode,port);return finish(m,pid,start,mode)

for ram in ('16M','64M'):
    with Machine(network=True,memory=ram,logname='poll-'+ram) as m:
        baseline=snapshot(m);run(m,'poll-basic');assert snapshot(m)==baseline
        pid,start=launch(m,'poll-timer');m.expect('POLL: timer waiting\n',start)
        reply=m.command('ps','PROCESS: list complete\nnotos> ')
        assert f'PROCESS WAIT: pid={pid} poll=1'.encode() in reply,reply
        m.command('int3','IDT: breakpoint OK; registers and flags preserved\nnotos> ')
        m.command('spawn worker','PROCESS: started pid=3')
        m.expect('PROCESS: pid=3 exit=42',start);finish(m,pid,start,'poll-timer')
        assert snapshot(m)==baseline
        with PollPeer() as peer:
            pid,start=launch(m,'poll',peer.port);m.expect('POLL: sockets waiting\n',start)
            reply=m.command('ps','PROCESS: list complete\nnotos> ')
            assert f'PROCESS WAIT: pid={pid} poll=1'.encode() in reply,reply
            m.command('echo poll-responsive','\npoll-responsive\nnotos> ')
            finish(m,pid,start,'poll')
        assert snapshot(m)==baseline
        with Server('reset') as peer:run(m,'poll-reset',peer.port)
        assert snapshot(m)==baseline
        with PollPeer() as peer:closed=peer.port
        run(m,'poll-refused',closed);assert snapshot(m)==baseline
        with PollPeer('backpressure') as peer:run(m,'poll-write',peer.port)
        assert snapshot(m)==baseline
        pid,start=launch(m,'poll-stdin');m.expect('POLL: stdin waiting\n',start)
        reply=m.command('ps','PROCESS: list complete\nnotos> ')
        assert f'PROCESS WAIT: pid={pid} poll=1'.encode() in reply,reply
        m.command(f'feed {pid} ready','INPUT: delivered\nnotos> ');m.expect('POLL: stdin consumed\n',start)
        m.command(f'feed-eof {pid}','INPUT: delivered\nnotos> ');finish(m,pid,start,'poll-stdin')
        assert snapshot(m)==baseline
        # All four processes can wait indefinitely; one wakeup never wakes other tables.
        waiting=[]
        for _ in range(4):
            pid,start=launch(m,'poll-stdin');m.expect('POLL: stdin waiting\n',start);waiting.append((pid,start))
        reply=m.command('ps','PROCESS: list complete\nnotos> ');assert reply.count(b'poll=1')==4,reply
        selected,start=waiting[1]
        m.command(f'feed {selected} only-selected','INPUT: delivered\nnotos> ');m.expect('POLL: stdin consumed\n',start)
        m.command(f'feed-eof {selected}','INPUT: delivered\nnotos> ');finish(m,selected,start,'poll-stdin')
        reply=m.command('ps','PROCESS: list complete\nnotos> ');assert reply.count(b'poll=1')==3,reply
        for pid,position in waiting:
            if pid==selected:continue
            m.command(f'kill {pid}','\nnotos> ');finish(m,pid,position,'poll-stdin',143)
        assert snapshot(m)==baseline
        pid,start=launch(m,'poll-wait');m.expect('POLL: infinite waiting\n',start)
        m.command(f'kill {pid}','\nnotos> ');finish(m,pid,start,'poll-wait',143)
        assert snapshot(m)==baseline
        pid,start=launch(m,'poll-fault');finish(m,pid,start,'poll-fault',134)
        m.expect('PROCESS FAULT: vector=6',start);assert snapshot(m)==baseline
        # Reused slot cannot inherit the cancelled vector or deadline.
        run(m,'poll-timer');assert snapshot(m)==baseline
    print('PASS:',ram,'poll multi-FD/duplicates/connect/data/FIN/RST/SO_ERROR/TX/stdin/timeouts/all-blocked/kill/fault/reuse')
