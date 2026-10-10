#!/usr/bin/env python3
"""Launch QEMU with a new host-CSPRNG firmware seed on every boot."""
import os
from pathlib import Path
import secrets
import subprocess
import sys
import tempfile

with tempfile.TemporaryDirectory(prefix='notos-entropy-') as directory:
    seed=Path(directory,'seed.bin')
    seed.write_bytes(secrets.token_bytes(64));seed.chmod(0o600)
    command=[os.environ.get('QEMU','qemu-system-i386'),
        '-cpu','max','-drive','format=raw,file=build/notos.img','-serial','stdio',
        '-netdev','user,id=net0','-device','rtl8139,netdev=net0',
        '-rtc','base=utc','-fw_cfg',f'name=opt/notos/entropy,file={seed}'] + sys.argv[1:]
    try:
        result=subprocess.run(command)
    except KeyboardInterrupt:
        sys.exit(130)
    sys.exit(result.returncode)
