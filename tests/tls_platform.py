#!/usr/bin/env python3
from harness import Machine
for entropy in (True, False):
    with Machine(entropy=entropy,logname='tls-platform-'+str(entropy)) as m:
        reply=m.command('tls','\nnotos> ')
        assert f'TLS: version=1.2/1.3 entropy={int(entropy)} rtc=1 roots='.encode() in reply,reply
        m.command('tls-selftest','TLS: SHA256/profile/RTC selftest PASS\nnotos> ')
        m.command('ring3-test','USER: private-CR3/TSS/syscall/cleanup PASS\nnotos> ')
        m.command('tls-selftest','TLS: SHA256/profile/RTC selftest PASS\nnotos> ')
        m.command('heap-test','HEAP: split/coalesce/calloc/OOM selftest PASS\nnotos> ')
print('PASS: firmware entropy presence/absence, RTC, SHA256, TLS12/13 profile and no FPU state dependency')
