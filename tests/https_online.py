#!/usr/bin/env python3
"""Optional real internet: guest TCP + guest TLS/X509, not host TLS proxying."""
import re
from harness import Machine
with Machine(network=True,logname='https-online') as m:
    m.command('ring3-test','USER: private-CR3/TSS/syscall/cleanup PASS\nnotos> ')
    reply=m.command('https example.com',' bytes; authenticated close_notify\n',timeout=50)
    assert b'TLS: verified example.com; TLS 1.2\n' in reply,reply
    assert re.search(rb'HTTPS: HTTP/1\.[01] 200',reply),reply
    m.command('net','NET: idle\n')
    m.command('heap-test','HEAP: split/coalesce/calloc/OOM selftest PASS\nnotos> ')
    print('PASS: real internet HTTPS, TLS12 DNS identity/chain/time verification and authenticated closure')
    print(reply.decode(errors='replace'))
