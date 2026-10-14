#!/usr/bin/env python3
"""Exercise M4 multi-call utilities (/system/box.elf), SYS_PROCESS_LIST (ps), byte-exact cp/mv across tmpfs and NotFS v3, and the ed line editor."""
from pathlib import Path
import re
import tempfile
from harness import Machine

def snapshot(m):
    frames = re.search(rb'free_frames=(\d+)', m.command('mem', '\nnotos> '))[1]
    heap = re.search(rb'HEAP: [^\n]+\n', m.command('heap', '\nnotos> '))[0]
    sockets = m.command('sockets', '\nnotos> ')
    assert b'open=0 queries=0' in sockets, sockets
    status = m.command('vfs', '\nnotos> ')
    assert b'files=0 nodes=0 pages=0 refs=0' in status and b'descriptions=0 pipes=0' in status, status
    return frames, heap

with tempfile.TemporaryDirectory(prefix='notos-box-disk-') as tmpdir:
    disk = Path(tmpdir, 'data.img')
    disk.write_bytes(b'\0' * 262144)

    with Machine(logname='utilities', data_disk=disk) as m:
        m.command('mkfs', 'FS: formatted dedicated data disk\nnotos> ')
        baseline = snapshot(m)

        # Enter user-space shell (/system/sh.elf) where applets resolve to /system/box.elf
        m.command('sh', 'sh$ ')

        # 1. Direct /system/box.elf invocation, echo (-n), true, false
        out = m.command('/system/box.elf echo -n "box_ok"; /system/box.elf echo "!"', 'sh$ ')
        assert b'box_ok!\n' in out, out
        out = m.command('true; echo t=$?; false; echo f=$?', 'sh$ ')
        assert b't=0\n' in out and b'f=1\n' in out, out

        # 2. mkdir, rmdir, touch, ls, ls -l, rm on both /tmp and /disk
        out = m.command('mkdir /tmp/sub /disk/dsub; echo mk=$?', 'sh$ ')
        assert b'mk=0\n' in out, out
        out = m.command('touch /tmp/sub/a.txt /disk/dsub/b.txt; ls /tmp/sub; ls -l /disk/dsub', 'sh$ ')
        assert b'a.txt\n' in out and b'b.txt\n' in out, out
        out = m.command('rm /tmp/sub/a.txt /disk/dsub/b.txt; rmdir /tmp/sub /disk/dsub; echo rmd=$?', 'sh$ ')
        assert b'rmd=0\n' in out, out

        # 3. ed line editor: a, i, c, d, p, n, =, w, q and line addresses (1, $, ., range)
        m.command('echo "a" > /tmp/ed.in; echo "line one" >> /tmp/ed.in; echo "line two" >> /tmp/ed.in; echo "line four" >> /tmp/ed.in; echo "." >> /tmp/ed.in', 'sh$ ')
        m.command('echo "3i" >> /tmp/ed.in; echo "line three" >> /tmp/ed.in; echo "." >> /tmp/ed.in; echo "2d" >> /tmp/ed.in; echo "1i" >> /tmp/ed.in', 'sh$ ')
        m.command('echo "header" >> /tmp/ed.in; echo "." >> /tmp/ed.in; echo "1,\\$p" >> /tmp/ed.in; echo "w /tmp/ed_out.txt" >> /tmp/ed.in; echo "q" >> /tmp/ed.in', 'sh$ ')
        out = m.command('ed < /tmp/ed.in', 'sh$ ')
        assert b'header\nline one\nline three\nline four\n' in out, out
        assert b'37\n' in out, out

        # Verify ed file load + edit in place on /disk
        m.command('cp /tmp/ed_out.txt /disk/ed_disk.txt', 'sh$ ')
        m.command('echo "2,3n" > /tmp/ed2.in; echo "\\$a" >> /tmp/ed2.in; echo "footer" >> /tmp/ed2.in; echo "." >> /tmp/ed2.in; echo "=" >> /tmp/ed2.in; echo "w" >> /tmp/ed2.in; echo "q" >> /tmp/ed2.in', 'sh$ ')
        out = m.command('ed /disk/ed_disk.txt < /tmp/ed2.in', 'sh$ ')
        assert b'37\n2\tline one\n3\tline three\n5\n44\n' in out, out
        out = m.command('cat /disk/ed_disk.txt', 'sh$ ')
        assert b'header\nline one\nline three\nline four\nfooter\n' in out, out

        # 4. head, tail, wc, grep (-v, -n, -i)
        out = m.command('head -n 2 /disk/ed_disk.txt', 'sh$ ')
        assert b'header\nline one\n' in out and b'line three' not in out, out
        out = m.command('tail -n 2 /disk/ed_disk.txt', 'sh$ ')
        assert b'line four\nfooter\n' in out and b'header' not in out, out
        out = m.command('wc /disk/ed_disk.txt; wc -l /disk/ed_disk.txt; wc -c /disk/ed_disk.txt', 'sh$ ')
        assert b'5 8 44\n5\n44\n' in out, out
        out = m.command('grep -i -n LINE /disk/ed_disk.txt', 'sh$ ')
        assert b'2:line one\n3:line three\n4:line four\n' in out, out
        out = m.command('grep -v line /disk/ed_disk.txt', 'sh$ ')
        assert b'header\nfooter\n' in out and b'line one' not in out, out

        # 5. Byte-exact cp and mv on /tmp and /disk, plus cross-filesystem mv (/tmp <-> /disk)
        out = m.command('cp /disk/ed_disk.txt /tmp/cp1.txt; cp /tmp/cp1.txt /disk/cp2.txt; wc -c /tmp/cp1.txt; wc -c /disk/cp2.txt', 'sh$ ')
        assert b'44\n44\n' in out, out
        out = m.command('mv /tmp/cp1.txt /tmp/mv1.txt; mv /tmp/mv1.txt /disk/mv_cross.txt; mv /disk/mv_cross.txt /tmp/mv_back.txt; cat /tmp/mv_back.txt', 'sh$ ')
        assert b'header\nline one\nline three\nline four\nfooter\n' in out, out
        out = m.command('ls /disk/mv_cross.txt; echo missing=$?', 'sh$ ')
        assert b'missing=1\n' in out, out

        # 6. env, date, uptime, ps (SYS_PROCESS_LIST), sleep, and kill
        out = m.command('export UTIL_VAR=box_val; env | grep UTIL_VAR', 'sh$ ')
        assert b'UTIL_VAR=box_val\n' in out, out
        out = m.command('date; date +%s; uptime', 'sh$ ')
        assert b'UTC\n' in out and b'up ' in out, out

        # Start a background sleep, inspect it in ps, then kill it
        m.command('sleep 30 &', 'sh$ ')
        out = m.command('ps', 'sh$ ')
        assert b'PID PPID STATE TICKS NAME\n' in out, out
        assert b'sh' in out and b'sleep' in out and b'ps' in out, out
        sleep_match = re.search(rb'(\d+)\s+\d+\s+[RS]\s+\d+\s+sleep', out)
        assert sleep_match is not None, out
        sleep_pid = sleep_match[1].decode('ascii')
        out = m.command(f'kill -9 {sleep_pid}; wait {sleep_pid}; echo k=$?', 'sh$ ')
        assert b'k=137\n' in out, out

        # Verify reboot/poweroff applets are registered in box.elf (dry-run -n)
        out = m.command('reboot -n; echo rb=$?; poweroff -n; echo po=$?', 'sh$ ')
        assert b'rb=0\n' in out and b'po=0\n' in out, out

        # Clean up temporary files before exit
        m.command('rm -f /tmp/ed.in /tmp/ed2.in /tmp/ed_out.txt /tmp/mv_back.txt /disk/ed_disk.txt /disk/cp2.txt', 'sh$ ')
        m.command('exit 0', 'notos> ')

        # Exact kernel resource recovery check
        assert snapshot(m) == baseline

print('PASS: multi-call utilities (/system/box.elf), SYS_PROCESS_LIST, byte-exact cp/mv, and ed line editor')
