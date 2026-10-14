#!/usr/bin/env python3
"""Exercise shell navigation, tmpfs CRUD and dedicated-disk persistence."""
import tempfile
from pathlib import Path
from harness import Machine


def command(machine, text):
    return machine.command(text, '\nnotos> ')


for memory in ('16M', '64M'):
    with Machine(memory=memory, logname='files-' + memory) as machine:
        assert b'\n/tmp\n' in command(machine, 'pwd')
        assert b'tmp/\ndisk/\ndev/\n' in command(machine, 'ls /')
        command(machine, 'write page.txt hello guest')
        assert b'FILE: page.txt bytes=11' in command(machine, 'ls')
        assert b'\nhello guest\n' in command(machine, 'cat ./page.txt')
        command(machine, 'touch page.txt')
        assert b'bytes=11' in command(machine, 'stat page.txt')
        command(machine, 'cd ..')
        assert b'\n/\n' in command(machine, 'pwd')
        assert b'\nhello guest\n' in command(machine, 'cat tmp/page.txt')
        command(machine, 'cd /tmp/missing')
        assert b'\n/\n' in command(machine, 'pwd')
        command(machine, 'cd ////tmp/./')
        command(machine, 'touch empty')
        assert b'bytes=0' in command(machine, 'stat empty')
        command(machine, 'rm ../tmp/page.txt')
        assert b'file not found' in command(machine, 'cat page.txt')
        command(machine, 'rm empty')
        assert b'files=0 nodes=0 pages=0 refs=0' in command(machine, 'vfs')
        assert b'capacity_bytes=262144' in command(machine, 'df')
        assert b'data disk unavailable' in command(machine, 'mkfs')
        command(machine, 'cd /dev')
        assert b'stdin\nstdout\nstderr\nconsole\nnull' in command(machine, 'ls')
        command(machine, 'cd')
        assert b'\n/\n' in command(machine, 'pwd')
    print(f'PASS: shell navigation, tmpfs CRUD, touch preservation and cleanup on {memory}')

with tempfile.TemporaryDirectory() as directory:
    disk = Path(directory, 'data.img')
    disk.write_bytes(bytes(256 * 1024))
    boot = Path('build/notos.img').read_bytes()
    with Machine(data_disk=disk, logname='files-disk-create') as machine:
        command(machine, 'mkfs')
        command(machine, 'cd /disk')
        command(machine, 'write persistent hello disk')
        command(machine, 'touch persistent')
        assert b'\nhello disk\n' in command(machine, 'cat persistent')
        command(machine, 'cd /tmp')
        assert b'FILE: persistent bytes=10' in command(machine, 'ls ../disk')
        assert b'bytes=10' in command(machine, 'stat /disk/persistent')
    with Machine(data_disk=disk, logname='files-disk-reboot') as machine:
        assert b'\n/disk\n' in command(machine, 'pwd')
        assert b'\nhello disk\n' in command(machine, 'cat ./persistent')
        command(machine, 'rm persistent')
        assert b'file not found' in command(machine, 'cat persistent')
    assert Path('build/notos.img').read_bytes() == boot
    print('PASS: shell disk paths persist across reboot; boot image unchanged')
