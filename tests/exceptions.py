#!/usr/bin/env python3
"""Actual CPU faults, not software INT substitutes for error-code exceptions."""
import re
import time
from harness import Machine
with Machine(logname='idt') as m:
    assert b'IDT: 256 gates installed\n' in m.output
    for _ in range(3):
        m.command('int3','IDT: breakpoint OK; registers and flags preserved\nnotos> ')
    m.command('echo after-int3','\nafter-int3\nnotos> ')
    print('PASS: IDT and repeated INT3; general registers/CF/DF/IF restored by IRET')
for command,vector,name,error in [
    ('fault divide',0,'Divide Error','00000000'),
    ('fault opcode',6,'Invalid Opcode','00000000'),
    ('fault gp',13,'General Protection','00000020'),
]:
    with Machine(logname='panic-'+str(vector)) as m:
        reply=m.command(command,'SYSTEM HALTED: restart required\n')
        assert f'PANIC: vector={vector} {name}\n'.encode() in reply,reply
        assert re.search(rb'ERROR=0x'+error.encode()+rb' EIP=0x[0-9a-f]{8} CS=0x00000008',reply),reply
        assert b'EAX=0x' in reply and b'SAVED_ESP=0x' in reply,reply
        # Fatal faults must halt, not reset/reboot or execute another command.
        m.p.stdin.write(b'echo must-not-run\n');m.p.stdin.flush()
        time.sleep(.15)
        assert m.p.poll() is None,'emulator exited (possible triple fault)'
        # Synchronize with halted CPU via QMP; then verify it is still alive.
        assert m.qmp('query-status')['running']
        assert b'HLT=1' in m.monitor('info registers'), 'CPU not halted'
        for key,_ in m.sel.select(.05):
            import os
            m.output.extend(os.read(key.fileobj.fileno(),65536))
        assert b'must-not-run' not in m.output
        m.screenshot('panic-'+str(vector))
        print(f'PASS: real #{vector} {name}, error code and halted diagnostic screen')
