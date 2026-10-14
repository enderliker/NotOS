#!/usr/bin/env python3
"""Exercise M3 user-space shell (/system/sh.elf): raw line editing, parser, builtins, pipelines, redirections, and slot exhaustion."""
import os
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

with tempfile.TemporaryDirectory(prefix='notos-sh-disk-') as tmpdir:
    disk = Path(tmpdir, 'data.img')
    disk.write_bytes(b'\0' * 262144)

    with Machine(logname='shell', data_disk=disk) as m:
        m.command('mkfs', 'FS: formatted dedicated data disk\nnotos> ')
        baseline = snapshot(m)

        # Launch /system/sh.elf in foreground via kernel 'sh' command.
        m.command('sh', 'sh$ ')

        # 1. Quoting, backslash escapes, $VAR, ${VAR}, and $?
        out = m.command('export GREET="hello world" NUM=42', 'sh$ ')
        out = m.command('echo \'$GREET\' "$GREET" \\$GREET ${NUM}x', 'sh$ ')
        assert b'$GREET hello world $GREET 42x\n' in out, out

        out = m.command('unset NUM; echo "num=$NUM"', 'sh$ ')
        assert b'num=\n' in out, out

        out = m.command('true; echo s0=$?; false; echo s1=$?', 'sh$ ')
        assert b's0=0\n' in out and b's1=1\n' in out, out

        out = m.command('sh -c "exit 37"; echo sub=$?', 'sh$ ')
        assert b'sub=37\n' in out, out

        # 2. Builtin cd and PWD tracking
        out = m.command('cd /system; echo pwd=$PWD; cd /; echo root=$PWD', 'sh$ ')
        assert b'pwd=/system\n' in out and b'root=/\n' in out, out

        out = m.command('cd /does_not_exist; echo cd_err=$?', 'sh$ ')
        assert b'sh: cd: /does_not_exist\n' in out and b'cd_err=1\n' in out, out

        # 3. Redirections (<, >, >>) to tmpfs (/tmp) and NotFS v3 (/disk)
        out = m.command('echo alpha > /tmp/sh_test.txt; echo beta >> /tmp/sh_test.txt; cat < /tmp/sh_test.txt', 'sh$ ')
        assert b'alpha\nbeta\n' in out, out

        out = m.command('echo disk_one > /disk/sh_disk.txt; echo disk_two >> /disk/sh_disk.txt; cat < /disk/sh_disk.txt', 'sh$ ')
        assert b'disk_one\ndisk_two\n' in out, out

        # 4. Multi-stage pipelines and exit-status propagation
        m.command('echo Apple > /tmp/lines.txt; echo banana >> /tmp/lines.txt; echo apricot >> /tmp/lines.txt', 'sh$ ')
        out = m.command('cat /tmp/lines.txt | grep -i -n ap', 'sh$ ')
        assert b'1:Apple\n3:apricot\n' in out, out

        out = m.command('cat /tmp/lines.txt | grep -v banana | wc -l', 'sh$ ')
        assert b'2\n' in out, out

        out = m.command('true | false; echo pipe_rc=$?', 'sh$ ')
        assert b'pipe_rc=1\n' in out, out

        # 5. Background jobs (&), stdin closed unless redirected, jobs, and wait
        out = m.command('cat &', 'sh$ ')
        out = m.command('wait; echo bg_cat=$?', 'sh$ ')
        assert b'bg_cat=1\n' in out, out

        m.command('sleep -m 150 &', 'sh$ ')
        out = m.command('jobs', 'sh$ ')
        assert b'[1] Running sleep\n' in out or b'[2] Running sleep\n' in out, out
        out = m.command('wait; echo bg_sleep=$?', 'sh$ ')
        assert b'bg_sleep=0\n' in out, out

        # 6. Slot exhaustion (4 slots total: sh.elf + 3 children max) and clean recovery
        out = m.command('sleep -m 300 | sleep -m 300 | sleep -m 300 | echo fourth; echo exhaust=$?', 'sh$ ')
        assert b'no free process slots\n' in out and b'exhaust=1\n' in out, out

        out = m.command('echo recovered | cat; echo rec_rc=$?', 'sh$ ')
        assert b'recovered\n' in out and b'rec_rc=0\n' in out, out

        # 7. Interactive raw-mode line editing (Tab completion, cursor left, history Up)
        start = len(m.output)
        m.p.stdin.write(b'ec\thello\x1b[D\x1b[D\x1b[D\x1b[D\x1b[DX\r')
        m.p.stdin.flush()
        out = m.expect('sh$ ', start)
        assert b'Xhello\n' in out, out

        # Up arrow recalls 'echo Xhello' from line_core history
        start = len(m.output)
        m.p.stdin.write(b'\x1b[A\r')
        m.p.stdin.flush()
        out = m.expect('sh$ ', start)
        assert b'Xhello\n' in out, out

        # Path Tab completion on /sys -> /system/
        start = len(m.output)
        m.p.stdin.write(b'echo /sys\t\r')
        m.p.stdin.flush()
        out = m.expect('sh$ ', start)
        assert b'/system/\n' in out, out

        # Exit user shell and verify foreground return to kernel shell
        m.command('exit 0', 'notos> ')

        # Clean up created files and verify zero resource leaks
        m.command('rm /tmp/sh_test.txt', 'notos> ')
        m.command('rm /tmp/lines.txt', 'notos> ')
        m.command('rm /disk/sh_disk.txt', 'notos> ')
        assert snapshot(m) == baseline

print('PASS: user-space shell (/system/sh.elf) raw editing, Tab completion, quoting/expansion, builtins, pipelines, redirections, background jobs, slot exhaustion and resource recovery')
