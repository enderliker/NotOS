#!/usr/bin/env python3
"""Hardware IRQ/PIT progress, EOI, HLT wakeup and deferred keyboard input."""
import re
import time
from harness import Machine

def uptime(m):
    reply=m.command('uptime','\nnotos> ')
    result=re.search(rb'UPTIME: ms=(\d+) ticks=(\d+) IF=(\d+)\n',reply)
    assert result,reply
    ms,ticks,enabled=map(int,result.groups())
    assert enabled==1, 'interrupts unexpectedly disabled'
    return ms,ticks

def irqs(m):
    reply=m.command('irqs','\nnotos> ')
    result=re.search(rb'IRQS: timer=(\d+) keyboard=(\d+) stray=(\d+) dropped=(\d+)\n',reply)
    assert result,reply
    return tuple(map(int,result.groups()))

def key(m,*codes):
    m.qmp('send-key', {'keys':[{'type':'qcode','data':c} for c in codes], 'hold-time':1})
    time.sleep(.015)

with Machine(logname='interrupts') as m:
    assert b'IRQ: PIC remapped; PIT ~1000 Hz\n' in m.output
    assert b'KEYBOARD: IRQ1 ready\n' in m.output
    before,ticks_before=uptime(m)
    time.sleep(.2)
    after,ticks_after=uptime(m)
    assert after-before>=100 and ticks_after-ticks_before>=100
    assert ticks_after>=after and ticks_after-after<10
    print('PASS: enabled IF, actual IRQ0 delivery and monotonic PIT clock')
    for kind,duration in [('sleep',0),('sleep',200),('busy',250),('sleep',20),('sleep',20),('sleep',20)]:
        reply=m.command(f'{kind} {duration}','\nnotos> ')
        match=re.search(rb'CLOCK: '+kind.encode()+rb' elapsed=(\d+) ms ticks=(\d+)\n',reply)
        assert match,reply
        elapsed,ticks=map(int,match.groups())
        assert duration<=elapsed<=duration+100,(elapsed,duration)
        assert abs(ticks-elapsed)<=3,(ticks,elapsed)
        uptime(m)
    print('PASS: HLT sleep wakes repeatedly; IRQ clock progresses during foreground busy work')
    for invalid in ['-1','5001','99999999999999999','1x','']:
        m.command('sleep '+invalid,'ERROR: duration must be 0..5000 ms\nnotos> ')
    timer,kbd,stray,dropped=irqs(m)
    assert timer>0 and stray==0 and dropped==0
    m.command('irq-test','IRQ: spurious 7/15 handled\nnotos> ')
    reply=m.command('irqs','\nnotos> ')
    assert b'SPURIOUS: irq7=1 irq15=1\n' in reply,reply
    m.command('sleep 20','\nnotos> ')
    after,_=uptime(m)
    assert after>before
    m.command('int3','IDT: breakpoint OK; registers and flags preserved\nnotos> ')
    uptime(m)
    print('PASS: spurious IRQ7/15 paths and EOI keep timer/IRET functioning')
    # Block shell processing for 1 second; IRQ1 must continue queueing keys.
    start=len(m.output)
    m.p.stdin.write(b'busy 1000\n');m.p.stdin.flush()
    m.expect('busy 1000\n',start)
    for code in ['e','c','h','o','spc','q','u','e','u','e','d','ret']:
        key(m,code)
    m.expect('\nqueued\nnotos> ',start,timeout=10)
    _,kbd_after,stray,dropped=irqs(m)
    assert kbd_after>kbd and stray==0 and dropped==0
    start=len(m.output)
    for code in ['e','c','h','o','spc']: key(m,code)
    key(m,'shift','a');key(m,'shift','1');key(m,'ret')
    m.expect('\nA!\nnotos> ',start)
    m.command('echo after-irq-tests','\nafter-irq-tests\nnotos> ')
    print('PASS: keyboard IRQ queue survives busy shell work, Shift and special characters')
