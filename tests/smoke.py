#!/usr/bin/env python3
import time
from harness import Machine
with Machine() as m:
    assert b'BOOT: protected mode OK\n' in m.output
    assert b'BOOT: kernel C OK\n' in m.output
    assert b'GUI: 320x200 OK\n' in m.output
    print('PASS: BIOS loader -> protected mode -> C kernel')
    initial=m.screenshot()
    def rgb(x,y): return initial[(y*320+x)*3:(y*320+x+1)*3]
    assert rgb(0,25)!=rgb(0,0), 'desktop background missing'
    assert rgb(8,30)!=rgb(10,32), 'window frame missing'
    assert rgb(10,32)!=rgb(10,46), 'titlebar missing'
    print('PASS: real VGA framebuffer, 320x200 desktop and window')
    m.command('echo serial-input', '\nserial-input\nnotos> ')
    m.command('about', '\nNotOS: own bootloader and kernel.\n32-bit x86; no Linux runtime.\nnotos> ')
    m.command('bad-command', '\nERROR: unknown command\nnotos> ')
    start=len(m.output)
    for key in ['e','c','h','o','spc','k','b','d','ret']:
        m.monitor('sendkey '+key)
        time.sleep(.08)
    m.expect('\nkbd\nnotos> ',start)
    print('PASS: PS/2 keyboard and serial command input')
    assert m.screenshot('desktop-after-input')!=initial
    start=len(m.output)
    m.monitor('sendkey f2')
    m.expect('notos> ',start)
    assert m.screenshot('desktop-clear')!=initial
    print('PASS: F2 clears the graphical terminal')
    m.command('run print 2 + 3 * 4', '\n14\nnotos> ')
    m.command('run let x = (2 + 3) * 4; print x', '\n20\nnotos> ')
    m.command('run print x - 7', '\n13\nnotos> ')
    m.command('run print 1 / 0', '\nLANG ERROR: division by zero\nnotos> ')
    m.command('run print 2147483647 + 1', '\nLANG ERROR: integer overflow\nnotos> ')
    m.command('run print missing', '\nLANG ERROR: undefined variable\nnotos> ')
    m.command('run print (2 + 3', '\nLANG ERROR: expected closing parenthesis\nnotos> ')
    m.command('run print ' + '-'*30 + '1', '\nLANG ERROR: expression too deep\nnotos> ')
    m.command('lang-reset', '\nLANG: variables cleared\nnotos> ')
    m.command('run print x', '\nLANG ERROR: undefined variable\nnotos> ')
    m.command('echo kernel-still-alive', '\nkernel-still-alive\nnotos> ')
    print('PASS: NotLang inside the actual guest kernel, including error recovery')
    reply=m.command('echo ' + 'x'*160 + 'echo must-not-run', '\nERROR: input too long; discarded\nnotos> ')
    assert b'\nmust-not-run\n' not in reply
    m.command('echo after-long-input', '\nafter-long-input\nnotos> ')
    print('PASS: oversized shell input is discarded without executing its tail')
