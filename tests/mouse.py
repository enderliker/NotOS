#!/usr/bin/env python3
import re
import time
from harness import Machine

def status(m):
    reply=m.command('mouse', '\nnotos> ')
    match=re.search(rb'MOUSE: ready=1 x=(\d+) y=(\d+) buttons=(\d+) packets=(\d+) dropped=0',reply)
    assert match,reply
    return tuple(map(int,match.groups()))
def move(m,x,y):
    old=status(m)
    m.qmp('input-send-event', {'events':[
        {'type':'rel','data':{'axis':'x','value':x-old[0]}},
        {'type':'rel','data':{'axis':'y','value':y-old[1]}}]})
    time.sleep(.08)
    new=status(m)
    assert (new[0],new[1])==(x,y),(old,new,(x,y))
def click(m):
    m.qmp('input-send-event', {'events':[{'type':'btn','data':{'button':'left','down':True}}]})
    time.sleep(.05)
    m.qmp('input-send-event', {'events':[{'type':'btn','data':{'button':'left','down':False}}]})
    time.sleep(.05)
with Machine(logname='mouse') as m:
    assert b'MOUSE: IRQ12 ready' in m.output
    original=m.screenshot('mouse-initial')
    move(m,197,10)
    start=len(m.output);click(m);m.expect('UI: clear\nnotos> ',start)
    move(m,222,10)
    start=len(m.output);click(m);m.expect('UI: files\nFS: list complete\nnotos> ',start)
    move(m,258,10)
    start=len(m.output);click(m);m.expect('PROCESS: list complete\nnotos> ',start)
    move(m,140,38);click(m)
    hidden=m.screenshot('mouse-minimized')
    assert hidden!=original
    m.command('echo while-hidden','\nwhile-hidden\nnotos> ')
    click(m)
    assert m.screenshot('mouse-restored')!=hidden
    m.command('sleep 20','\nnotos> ')
    m.command('int3','IDT: breakpoint OK; registers and flags preserved\nnotos> ')
    for key in ['e','c','h','o','spc','o','k','ret']:
        m.monitor('sendkey '+key);time.sleep(.05)
    m.expect('\nok\nnotos> ')
    assert status(m)[3]>=10
    print('PASS: actual IRQ12 mouse, pointer, toolbar clicks, minimize/restore, keyboard coexists')
