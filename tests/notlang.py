#!/usr/bin/env python3
import re
import tempfile
from pathlib import Path
from harness import Machine

def execute(m,command,pid,status,output):
    start=len(m.output)
    m.command(command,f'NotLang: started ring3 pid={pid}\nnotos> ')
    reply=m.expect(f'PROCESS: pid={pid} exit={status}',start)
    for text in output: assert text.encode() in reply,reply

with tempfile.TemporaryDirectory() as directory:
    disk=Path(directory,'data.img');disk.write_bytes(bytes(128*1024))
    with Machine(data_disk=disk,logname='notlang') as m:
        before=m.command('mem','\nnotos> ')
        free=re.search(rb'free_frames=(\d+)',before)[1]
        execute(m,'run-user let x = 1; repeat 3 { let x = x * 2; }; print x',1,0,['USER OUT: 8\n'])
        execute(m,'run-user repeat 2 { repeat 2 { print -3; }; }',2,0,['USER OUT: -3\n'])
        execute(m,'run-user print 1 / 0',3,65,['NotLang: division by zero'])
        execute(m,'run-user print 2147483647 + 1',4,65,['NotLang: integer overflow'])
        m.command('run-user print absent','NotLang COMPILE ERROR: undefined variable\nnotos> ')
        m.command('mkfs','FS: formatted dedicated data disk\nnotos> ')
        m.command('write hello.nl let x = 40; print x + 2','FS: file updated\nnotos> ')
        execute(m,'exec hello.nl',5,0,['USER OUT: 42\n'])
        assert re.search(rb'free_frames=(\d+)',m.command('mem','\nnotos> '))[1]==free
        m.command('heap-test','HEAP: split/coalesce/calloc/OOM selftest PASS\nnotos> ')
    with Machine(data_disk=disk,logname='notlang-reboot') as m:
        execute(m,'exec hello.nl',1,0,['USER OUT: 42\n'])
        m.command('exec missing.nl','NotLang ERROR: file unavailable\nnotos> ')
        m.command('int3','IDT: breakpoint OK; registers and flags preserved\nnotos> ')
    print('PASS: ring3 NotLang VM, arithmetic, repeat/nested loops, runtime errors and resource cleanup')
    print('PASS: source file persisted, rebooted, compiled and executed outside kernel')
