#!/usr/bin/env python3
"""Persist across actual VM restarts, corrupt the latest bank and recover the previous."""
import struct
import tempfile
from pathlib import Path
from harness import Machine

with Machine(logname='storage-missing') as m:
    m.command('mkfs', 'FS ERROR: data disk unavailable\nnotos> ')
with tempfile.TemporaryDirectory() as directory:
    path = Path(directory, 'data.img')
    path.write_bytes(bytes(512 * 256))
    boot_before = Path('build/notos.img').read_bytes()
    with Machine(data_disk=path, logname='storage-create') as m:
        assert b'FS: unformatted' in m.output
        m.command('mkfs', 'FS: formatted dedicated data disk\nnotos> ')
        m.command('write hello.txt original', 'FS: transaction committed\nnotos> ')
        m.command('write hello.txt updated', 'FS: transaction committed\nnotos> ')
        m.command('cat hello.txt', '\nupdated\nnotos> ')
        m.command('write ../bad rejected', 'FS ERROR: invalid name\nnotos> ')
        m.command('ls', 'FS: list complete\nnotos> ')
    with Machine(data_disk=path, logname='storage-reboot') as m:
        assert b'FS: NotFS mounted' in m.output
        m.command('cat hello.txt', '\nupdated\nnotos> ')
    data = bytearray(path.read_bytes())
    generations = [struct.unpack_from('<I', data, bank * 33 * 512 + 8)[0] for bank in (0, 1)]
    latest = generations.index(max(generations))
    data[latest * 33 * 512 + 550] ^= 0xff
    path.write_bytes(data)
    with Machine(data_disk=path, logname='storage-recovery') as m:
        m.command('cat hello.txt', '\noriginal\nnotos> ')
        m.command('rm hello.txt', 'FS: transaction committed\nnotos> ')
        m.command('cat hello.txt', 'FS ERROR: file not found\nnotos> ')
        for i in range(8): m.command(f'write f{i} data', 'FS: transaction committed\nnotos> ')
        m.command('write overflow data', 'FS ERROR: directory full\nnotos> ')
        m.command('mkfs', 'FS: formatted dedicated data disk\nnotos> ')
    with Machine(data_disk=path, logname='storage-format-reboot') as m:
        reply = m.command('ls', 'FS: list complete\nnotos> ')
        assert b'FILE:' not in reply
    assert Path('build/notos.img').read_bytes() == boot_before
    print('PASS: ATA data disk, persistent CRUD, reboot, CRC fallback, capacity and format')
    print('PASS: boot image unchanged; missing disk never formats the boot drive')
