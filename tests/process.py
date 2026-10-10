#!/usr/bin/env python3
import re
from harness import Machine

def snapshot(m):
    frames = re.search(rb'free_frames=(\d+)', m.command('mem', '\nnotos> '))[1]
    heap = re.search(rb'HEAP: [^\n]+\n', m.command('heap', '\nnotos> '))[0]
    return frames, heap

with Machine(logname='process', network=True) as m:
    baseline = snapshot(m)
    start = len(m.output)
    # Deliver as one serial batch, so all four slots are occupied before a quantum.
    m.p.stdin.write(b'spawn spin\nspawn spin\nspawn spin\nspawn spin\nspawn spin\nps\n'); m.p.stdin.flush()
    reply = m.expect('PROCESS: list complete\nnotos> ', start)
    assert b'PROCESS ERROR: no slot or memory\n' in reply
    assert len(re.findall(rb'PROCESS: pid=\d+ name=spin ticks=0', reply)) == 4, reply
    # Shell responds while tasks are alive; cancel one and reclaim its resources.
    reply = m.command('echo responsive', '\nresponsive\nnotos> ')
    m.command('kill 2', 'PROCESS: pid=2 exit=143')
    m.command('kill 999', 'PROCESS ERROR: pid not found\nnotos> ')
    m.expect('PROCESS: pid=4 exit=124', start)
    assert snapshot(m) == baseline
    start = len(m.output)
    m.p.stdin.write(b'spawn fault\nspawn worker\n'); m.p.stdin.flush()
    m.expect('PROCESS: pid=5 exit=142', start)
    m.expect('PROCESS: pid=6 exit=42', start)
    assert snapshot(m) == baseline
    m.command('ps', 'PROCESS: list complete\nnotos> ')
    m.command('int3', 'IDT: breakpoint OK; registers and flags preserved\nnotos> ')
    m.command('multitask-run four', 'SCHED: preemptive multitask/contexts/cleanup PASS\nnotos> ')
    print('PASS: background tasks, live shell, four slots, cancellation, faults and auto-reaping')
