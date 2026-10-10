#!/usr/bin/env python3
"""Boot real ring-3 batches; inspect context switches and resource restoration."""
import re
from harness import Machine

PASS = 'SCHED: preemptive multitask/contexts/cleanup PASS\nnotos> '
RECORD = rb'TASK: id=(\d+) exit=(\d+) ticks=(\d+) dispatches=(\d+) preemptions=(\d+) cr3=(\d+) stack=(\d+) counter=(\d+)\n'
SCENARIOS = {
    'workers': [42, 42],
    'four': [42, 42, 42, 42],
    'yield': [42, 42],
    'fault': [42, 142, 42],
    'budget': [42, 124],
    'fpu': [135, 42],
}

def snapshot(machine):
    frames = re.search(rb'free_frames=(\d+)', machine.command('mem', '\nnotos> ')).group(1)
    heap = re.search(rb'HEAP: [^\n]+\n', machine.command('heap', '\nnotos> ')).group(0)
    return frames, heap

def check_batch(machine, scenario):
    before = snapshot(machine)
    reply = machine.command('multitask-run ' + scenario, PASS)
    records = [list(map(int, fields)) for fields in re.findall(RECORD, reply)]
    expected = SCENARIOS[scenario]
    assert len(records) == len(expected), reply
    assert [r[0] for r in records] == list(range(1, len(expected) + 1)), reply
    assert [r[1] for r in records] == expected, reply
    assert len({r[5] for r in records}) == len(records), reply
    assert len({r[6] for r in records}) == len(records), reply
    for task_id, status, ticks, dispatches, preemptions, root, stack, counter in records:
        assert dispatches > 0 and root and stack, reply
        if status == 42:
            assert counter > 0, reply
            if scenario in ('workers', 'four', 'fault', 'budget'):
                assert ticks > 0 and dispatches >= 2 and preemptions > 0, reply
        if status == 124:
            assert ticks == 250, reply
    stats = re.search(rb'SCHED: tasks=(\d+) switches=(\d+) preemptions=(\d+) yields=(\d+)', reply)
    assert stats and int(stats[1]) == len(expected), reply
    events = [tuple(map(int, fields)) for fields in re.findall(rb'SWITCH: from=(\d+) to=(\d+) cause=(\d+)\n', reply)]
    assert events[0] == (0, 1, 0), reply
    # Finished tasks must never be selected again, even when another task faults.
    finished = set()
    for old, new, cause in events:
        if cause in (2, 3, 4):
            finished.add(old)
        assert not new or new not in finished, reply
    if scenario in ('workers', 'four'):
        assert (1, 2, 1) in events, reply
        assert (len(expected), 1, 1) in events, reply
    if scenario == 'yield':
        assert int(stats[4]) > 0 and (1, 2, 5) in events and (2, 1, 5) in events, reply
    else:
        assert int(stats[4]) == 0, reply
    if scenario == 'fault':
        assert b'TASK FAULT: id=2 vector=14 error=5 address=65536\n' in reply, reply
        assert any(old == 2 and cause == 3 for old, _, cause in events), reply
    if scenario == 'fpu':
        assert b'TASK FAULT: id=1 vector=7 error=0 address=0\n' in reply, reply
        assert (1, 2, 3) in events, reply
    if scenario == 'budget':
        assert (2, 0, 4) in events, reply
    assert snapshot(machine) == before, reply

def check_partial_creation_oom(machine):
    initial = snapshot(machine)
    largest = int(re.search(rb'largest_free=(\d+)', initial[1])[1])
    # Zero or two kernel stacks fit: exercise first-allocation and partial rollback.
    for remaining in (8192, 40000):
        size = largest - remaining
        machine.command(f'alloc {size}', f'HEAP: slot=0 bytes={size}\nnotos> ')
        allocated = snapshot(machine)
        reply = machine.command('multitask-run four', 'SCHED ERROR: multitask test failed\nnotos> ')
        assert b'TASK: id=' not in reply, reply
        assert snapshot(machine) == allocated, reply
        machine.command('free 0', 'HEAP: freed slot=0\nnotos> ')
        assert snapshot(machine) == initial
    check_batch(machine, 'four')

for memory in ('16M', '32M', '64M'):
    with Machine(logname='multitask-' + memory, memory=memory) as machine:
        before = snapshot(machine)
        machine.command('multitask-run unknown', 'SCHED ERROR: choose workers/four/yield/fault/budget/fpu\nnotos> ')
        assert snapshot(machine) == before
        for scenario in SCENARIOS:
            check_batch(machine, scenario)
        if memory == '32M':
            for _ in range(2):
                for scenario in SCENARIOS:
                    check_batch(machine, scenario)
            machine.command('multitask-test', PASS)
        check_partial_creation_oom(machine)
        assert snapshot(machine) == before
        machine.command('int3', 'IDT: breakpoint OK; registers and flags preserved\nnotos> ')
        machine.command('sleep 20', '\nnotos> ')
        machine.command('heap-test', 'HEAP: split/coalesce/calloc/OOM selftest PASS\nnotos> ')
        print(f'PASS {memory}: timer preemption, four slots, yield, fault isolation, CPU budget, FPU trap')
        print(f'PASS {memory}: private CR3/TSS stacks, registers/DF, finished-task exclusion, invalid batches rejected, OOM rollback, no resource leaks')
