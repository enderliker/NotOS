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
        m.command('write hello.txt original', 'FS: file updated\nnotos> ')
        m.command('write hello.txt updated', 'FS: file updated\nnotos> ')
        m.command('cat hello.txt', '\nupdated\nnotos> ')
        m.command('write ../bad rejected', 'FS ERROR: file not found\nnotos> ')
        m.command('ls', 'FS: list complete\nnotos> ')
    with Machine(data_disk=path, logname='storage-reboot') as m:
        assert b'FS: NotFS mounted' in m.output
        m.command('cat hello.txt', '\nupdated\nnotos> ')
    data = bytearray(path.read_bytes())
    generations = [struct.unpack_from('<I', data, bank * 128 * 512 + 8)[0] for bank in (0, 1)]
    latest = generations.index(max(generations))
    data[latest * 128 * 512 + 550] ^= 0xff
    path.write_bytes(data)
    with Machine(data_disk=path, logname='storage-recovery') as m:
        m.command('cat hello.txt', '\noriginal\nnotos> ')
        m.command('rm hello.txt', 'FS: file removed\nnotos> ')
        m.command('cat hello.txt', 'FS ERROR: file not found\nnotos> ')
        for i in range(24): m.command(f'write f{i} data', 'FS: file updated\nnotos> ')
        m.command('write overflow data', 'FS ERROR: no space\nnotos> ')
        m.command('mkfs', 'FS: formatted dedicated data disk\nnotos> ')
    with Machine(data_disk=path, logname='storage-format-reboot') as m:
        reply = m.command('ls', 'FS: list complete\nnotos> ')
        assert b'FILE:' not in reply
    assert Path('build/notos.img').read_bytes() == boot_before
    print('PASS: ATA data disk, persistent CRUD, reboot, CRC fallback, capacity and format')
    print('PASS: boot image unchanged; missing disk never formats the boot drive')

# Mount and migrate an actual v1 image, preserving both old banks until v2 is verified.
import zlib
with tempfile.TemporaryDirectory() as directory:
    path=Path(directory,'legacy.img')
    data=bytearray(128*1024)
    struct.pack_into('<IIII',data,0,0x5346544e,1,5,0)
    data[512:519]=b'legacy\0'
    struct.pack_into('<I',data,536,6)
    data[540:546]=b'old-v1'
    struct.pack_into('<I',data,12,zlib.crc32(data[:33*512]))
    original=bytes(data[:66*512]);path.write_bytes(data)
    with Machine(data_disk=path,logname='storage-migration') as machine:
        assert b'FS: migrated NotFS v1 to v2' in machine.output
        machine.command('cat legacy','\nold-v1\nnotos> ')
    migrated=path.read_bytes()
    assert migrated[:66*512]==original
    assert struct.unpack_from('<I',migrated,128*512+4)[0]==2
    with Machine(data_disk=path,logname='storage-migration-reboot') as machine:
        assert b'migrated NotFS' not in machine.output
        machine.command('cat legacy','\nold-v1\nnotos> ')
    print('PASS: real v1 disk migrates transactionally to v2 and survives reboot with old banks intact')
