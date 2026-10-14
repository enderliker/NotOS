#!/usr/bin/env python3
"""Exercise M1 process composition: SYS_SPAWN, SYS_WAIT, SYS_GETPPID, SYS_PIPE, SYS_KILL, and envp."""
import re
from harness import Machine

def snapshot(m):
    frames = re.search(rb'free_frames=(\d+)', m.command('mem', '\nnotos> '))[1]
    heap = re.search(rb'HEAP: [^\n]+\n', m.command('heap', '\nnotos> '))[0]
    sockets = m.command('sockets', '\nnotos> ')
    assert b'open=0 queries=0' in sockets, sockets
    status = m.command('vfs', '\nnotos> ')
    assert b'files=0 nodes=0 pages=0 refs=0' in status and b'descriptions=0 pipes=0' in status, status
    return frames, heap

def run_pipeline(m, command_text):
    start = len(m.output)
    reply = m.command(command_text, 'notos> ')
    pid = int(re.search(rb'started pid=(\d+)', reply)[1])
    m.expect(f'PROCESS: pid={pid} exit=0', start, timeout=60)
    out = bytes(m.output[start:])
    assert b'LIBC FAIL:' not in out and b'PROCESS FAULT:' not in out and b'PANIC:' not in out, out
    assert b'PIPELINE: spawn/wait/pipe/kill/env/orphan/slots PASS' in out, out
    assert b'LIBC: atexit PASS' in out and b'LIBC: fini-array PASS' in out, out

for ram in ('16M', '64M'):
    with Machine(network=True, memory=ram, logname='pipeline-' + ram) as m:
        baseline = snapshot(m)
        run_pipeline(m, 'libc-run pipeline')
        assert snapshot(m) == baseline
        run_pipeline(m, 'spawn-elf /system/libc.elf pipeline')
        assert snapshot(m) == baseline
        m.command('ps', 'PROCESS: list complete\nnotos> ')
        print('PASS', ram, ': 2-stage 16KiB checksummed pipeline, spawn/fd_map/posix_spawn, waitpid/WNOHANG, envp, zombie/orphan cleanup, kill blocked tasks, 4-slot exhaustion and exact resource recovery')
