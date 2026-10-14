#!/usr/bin/env python3
"""Launch QEMU with persistent data and a fresh firmware entropy seed."""
import os
from pathlib import Path
import secrets
import subprocess
import sys
import tempfile


def data_drive(arguments):
    if '-drive' in arguments:
        return []
    disk = Path(os.environ.get('NOTOS_DATA_DISK', 'data.img'))
    try:
        with disk.open('xb') as image:
            image.truncate(256 * 1024)
    except FileExistsError:
        pass
    return ['-drive', f'if=ide,index=1,format=raw,file={disk}']


def main():
    arguments = sys.argv[1:]
    drive = data_drive(arguments)
    with tempfile.TemporaryDirectory(prefix='notos-entropy-') as directory:
        seed = Path(directory, 'seed.bin')
        seed.write_bytes(secrets.token_bytes(64))
        seed.chmod(0o600)
        command = [
            os.environ.get('QEMU', 'qemu-system-i386'),
            '-cpu', 'max', '-drive', 'format=raw,file=build/notos.img',
            '-serial', 'stdio', '-netdev', 'user,id=net0',
            '-device', 'rtl8139,netdev=net0', '-rtc', 'base=utc',
            '-fw_cfg', f'name=opt/notos/entropy,file={seed}',
        ] + drive + arguments
        try:
            return subprocess.run(command).returncode
        except KeyboardInterrupt:
            return 130


if __name__ == '__main__':
    sys.exit(main())
