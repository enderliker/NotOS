#!/usr/bin/env python3
import re
from harness import Machine
PASS = 'SCHED: preemptive multitask/contexts/cleanup PASS\nnotos> '
with Machine(logname='multitask') as machine:
    before = machine.command('mem', '\nnotos> ')
    free = re.search(rb'free_frames=(\d+)', before).group(1)
    heap = machine.command('heap', '\nnotos> ')
    heap_line = re.search(rb'HEAP: [^\n]+\n', heap).group(0)
    for _ in range(3):
        reply = machine.command('multitask-test', PASS)
        records = re.findall(rb'TASK: id=(\d+) exit=(\d+) ticks=(\d+) dispatches=(\d+) preemptions=(\d+) cr3=(\d+) stack=(\d+) counter=(\d+)\n', reply)
        assert len(records) == 2, reply
        assert records[0][5] != records[1][5] and records[0][6] != records[1][6], reply
        for record in records:
            _, status, ticks, dispatches, preemptions, _, _, counter = map(int, record)
            assert status == 42 and ticks > 0 and dispatches >= 2 and preemptions > 0 and counter > 0, reply
        assert b'SWITCH: from=1 to=2 cause=1\n' in reply, reply
        assert b'SWITCH: from=2 to=1 cause=1\n' in reply, reply
        assert re.search(rb'free_frames=(\d+)', machine.command('mem', '\nnotos> ')).group(1) == free
        assert heap_line in machine.command('heap', '\nnotos> ')
    machine.command('int3', 'IDT: breakpoint OK; registers and flags preserved\nnotos> ')
    machine.command('sleep 20', '\nnotos> ')
    machine.command('heap-test', 'HEAP: split/coalesce/calloc/OOM selftest PASS\nnotos> ')
    print('PASS: timer-only preemption, alternating CR3/TSS stacks, register/DF canaries')
    print('PASS: repeated batches restore frames, heap, kernel context and interrupt delivery')
