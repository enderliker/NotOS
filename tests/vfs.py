#!/usr/bin/env python3
"""Exercise actual userspace file syscalls and libc, including blocked-input wakeup."""
import re
import tempfile
from pathlib import Path
from harness import Machine

def snapshot(m):
    frames=re.search(rb'free_frames=(\d+)',m.command('mem','\nnotos> '))[1]
    heap=re.search(rb'HEAP: [^\n]+\n',m.command('heap','\nnotos> '))[0]
    status=m.command('vfs','\nnotos> ')
    assert b'files=0 nodes=0 pages=0 refs=0' in status and b'descriptions=0' in status,status
    return frames,heap

def launch(m,mode):
    start=len(m.output)
    reply=m.command('libc-run '+mode,'notos> ')
    pid=int(re.search(rb'LIBC: started pid=(\d+)',reply)[1])
    return pid,start

def finish(m,pid,start,status=0):
    m.expect(f'PROCESS: pid={pid} exit={status}',start,timeout=45)
    reply=bytes(m.output[start:])
    assert b'LIBC FAIL:' not in reply,reply
    if not status:assert b'PROCESS FAULT:' not in reply,reply
    return reply

for ram in ('16M','64M'):
    with Machine(memory=ram,logname='vfs-'+ram) as m:
        baseline=snapshot(m)
        pid,start=launch(m,'files');reply=finish(m,pid,start)
        assert b'VFS: binary stdio/seek/append/dup/unlink/errors/FD-limit PASS' in reply,reply
        assert snapshot(m)==baseline
        start=len(m.output);m.p.stdin.write(b'libc-run files\n'*4);m.p.stdin.flush()
        for pid in range(2,6):finish(m,pid,start)
        assert bytes(m.output[start:]).count(b'VFS: binary stdio/seek/append/dup/unlink/errors/FD-limit PASS')==4
        assert snapshot(m)==baseline
        pid,start=launch(m,'stdin');m.expect('VFS: stdin waiting\n',start)
        reply=m.command('ps','PROCESS: list complete\nnotos> ')
        assert f'PROCESS WAIT: pid={pid} stdin=1'.encode() in reply,reply
        m.command('int3','IDT: breakpoint OK; registers and flags preserved\nnotos> ')
        m.command(f'feed {pid} hello-vfs','INPUT: delivered\nnotos> ')
        m.expect('VFS: stdin received\n',start)
        m.command(f'feed-eof {pid}','INPUT: delivered\nnotos> ')
        assert b'VFS: blocking stdin/EOF PASS' in finish(m,pid,start)
        assert snapshot(m)==baseline
        pid,start=launch(m,'hold');m.expect('VFS: orphan held\n',start)
        # The inode is unlinked, but is retained until the final descriptor reference closes.
        status=m.command('vfs','\nnotos> ')
        assert b'files=0 nodes=1 pages=3 refs=1' in status and b'descriptions=4' in status,status
        for _ in range(2):m.command(f'feed {pid} '+('x'*100),'INPUT: delivered\nnotos> ')
        m.command(f'feed {pid} '+('x'*100),'INPUT ERROR: queue full or EOF\nnotos> ')
        reply=m.command(f'kill {pid}','\nnotos> ');assert f'PROCESS: pid={pid} exit=143'.encode() in reply,reply
        m.command(f'feed {pid} stale','INPUT ERROR: pid not found\nnotos> ')
        assert snapshot(m)==baseline
        pid,start=launch(m,'fault');finish(m,pid,start,134)
        reply=m.expect('PROCESS FAULT: vector=6',start)
        assert b'PROCESS FAULT: vector=6' in reply,reply
        assert snapshot(m)==baseline
        pid,start=launch(m,'stdin');m.expect('VFS: stdin waiting\n',start)
        reply=m.command(f'kill {pid}','\nnotos> ');assert f'PROCESS: pid={pid} exit=143'.encode() in reply,reply
        assert snapshot(m)==baseline
        pid,start=launch(m,'files');finish(m,pid,start);assert snapshot(m)==baseline
        readers=[]
        for _ in range(4):
            reader,position=launch(m,'stdin');m.expect('VFS: stdin waiting\n',position);readers.append((reader,position))
        selected,position=readers[1]
        m.command(f'feed {selected} hello-vfs','INPUT: delivered\nnotos> ')
        m.expect('VFS: stdin received\n',position)
        m.command(f'feed-eof {selected}','INPUT: delivered\nnotos> ');finish(m,selected,position)
        reply=m.command('ps','PROCESS: list complete\nnotos> ')
        assert reply.count(b'stdin=1')==3,reply
        for reader,_ in readers:
            if reader!=selected:
                reply=m.command(f'kill {reader}','\nnotos> ');assert f'PROCESS: pid={reader} exit=143'.encode() in reply
        assert snapshot(m)==baseline
        print('PASS',ram,': binary stdio/FD isolation/dup/append/unlink/seek, blocked stdin/EOF, kill/fault reclamation and queue limits')
with tempfile.TemporaryDirectory() as directory:
    disk=Path(directory,'data.img');disk.write_bytes(bytes(512*256))
    original=Path('build/notos.img').read_bytes()
    with Machine(data_disk=disk,logname='vfs-notfs') as m:
        m.command('mkfs','FS: formatted dedicated data disk\nnotos> ')
        m.command('write hello.txt from-disk','FS: transaction committed\nnotos> ')
        baseline=snapshot(m);pid,start=launch(m,'disk')
        m.expect('VFS: disk snapshot waiting\n',start)
        m.command('write hello.txt new-data!','FS: transaction committed\nnotos> ')
        assert b'VFS: NotFS read-only snapshots PASS' in finish(m,pid,start)
        assert snapshot(m)==baseline
    with Machine(data_disk=disk,logname='vfs-notfs-reboot') as m:
        m.command('cat hello.txt','\nnew-data!\nnotos> ')
        assert snapshot(m)
    assert Path('build/notos.img').read_bytes()==original
    print('PASS: read-only NotFS mount, immutable open snapshots, new opens see updates, reboot persistence and boot image unchanged')
