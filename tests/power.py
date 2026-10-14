#!/usr/bin/env python3
"""Verify SYS_POWER (reboot, poweroff, halt) and NotFS synchronization across power transitions."""
from pathlib import Path
import tempfile
from harness import Machine

with tempfile.TemporaryDirectory(prefix='notos-power-') as tmp:
    disk = Path(tmp, 'data.img')
    disk.write_bytes(b'\0' * (256 * 1024))

    with Machine(logname='power-reboot', data_disk=disk, allow_reboot=True) as m:
        assert m.output.count(b'BOOT: 512 KiB loader + 768 KiB high image verified\n') == 1
        m.command('sh', 'sh$ ')
        out = m.command('reboot -bad; echo rc=$?', 'sh$ ')
        assert b'reboot: power control failed\n' in out, out
        assert b'rc=1\n' in out, out
        m.command('exit', 'notos> ')
        print('PASS: SYS_POWER rejects invalid power actions with EINVAL')

        m.command('mkfs', 'FS: formatted dedicated data disk\nnotos> ')
        m.command('write /disk/survive.txt synced-before-reboot', 'FS: file updated\nnotos> ')
        start = len(m.output)
        m.p.stdin.write(b'reboot\n')
        m.p.stdin.flush()
        m.expect('POWER: rebooting\n', start)
        m.expect('BOOT: 512 KiB loader + 768 KiB high image verified\n', start)
        m.expect('notos> ', start)
        assert m.output.count(b'BOOT: 512 KiB loader + 768 KiB high image verified\n') == 2
        m.command('cat /disk/survive.txt', '\nsynced-before-reboot\nnotos> ')
        print('PASS: kernel reboot syncs NotFS and reaches boot signature a second time')

        start = len(m.output)
        m.p.stdin.write(b'sh -c reboot\n')
        m.p.stdin.flush()
        m.expect('POWER: rebooting\n', start)
        m.expect('BOOT: 512 KiB loader + 768 KiB high image verified\n', start)
        m.expect('notos> ', start)
        assert m.output.count(b'BOOT: 512 KiB loader + 768 KiB high image verified\n') == 3
        m.command('cat /disk/survive.txt', '\nsynced-before-reboot\nnotos> ')
        print('PASS: user-space /system/box.elf reboot via SYS_POWER reaches boot signature')

        start = len(m.output)
        m.p.stdin.write(b'sh -c poweroff\n')
        m.p.stdin.flush()
        rc = m.wait_exit(timeout=10)
        assert rc == 0, f'Expected clean QEMU exit (0), got {rc}'
        assert b'POWER: powering off\n' in m.output[start:], m.output[start:]
        print('PASS: user-space /system/box.elf poweroff exits QEMU cleanly via ACPI shutdown port')

    with Machine(logname='power-halt') as m:
        start = len(m.output)
        m.p.stdin.write(b'halt\n')
        m.p.stdin.flush()
        m.expect('POWER: system halted\n', start)
        print('PASS: kernel halt masks IRQs and halts the processor cleanly')

    with Machine(logname='power-off', data_disk=disk) as m:
        m.command('cat /disk/survive.txt', '\nsynced-before-reboot\nnotos> ')
        start = len(m.output)
        m.p.stdin.write(b'poweroff\n')
        m.p.stdin.flush()
        rc = m.wait_exit(timeout=10)
        assert rc == 0, f'Expected clean QEMU exit (0), got {rc}'
        assert b'POWER: powering off\n' in m.output[start:], m.output[start:]
        print('PASS: kernel poweroff exits QEMU cleanly')
