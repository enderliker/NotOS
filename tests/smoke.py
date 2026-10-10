#!/usr/bin/env python3
import os, selectors, subprocess, time
from pathlib import Path
p = subprocess.Popen([os.environ.get('QEMU', 'qemu-system-i386'), '-machine', 'pc', '-m', '32M',
    '-drive', 'format=raw,file=build/notos.img', '-display', 'none',
    '-monitor', 'none', '-serial', 'stdio', '-no-reboot'],
    stdin=subprocess.PIPE, stdout=subprocess.PIPE, stderr=subprocess.PIPE)
output = bytearray()
sel = selectors.DefaultSelector()
sel.register(p.stdout, selectors.EVENT_READ)
try:
    deadline = time.monotonic() + 10
    while time.monotonic() < deadline:
        for key, _ in sel.select(0.1):
            output.extend(os.read(key.fileobj.fileno(), 4096))
        if b'BOOT: kernel C OK\n' in output:
            break
        if p.poll() is not None:
            raise AssertionError('QEMU exited before boot')
    assert b'BOOT: protected mode OK\n' in output, output
    assert b'BOOT: kernel C OK\n' in output, output
    print('PASS: BIOS loader -> protected mode -> C kernel -> serial output')
finally:
    p.terminate()
    p.wait(timeout=5)
    Path('build/smoke.log').write_bytes(output)
    sel.close()
