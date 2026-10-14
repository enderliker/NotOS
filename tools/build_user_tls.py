#!/usr/bin/env python3
"""Build the pinned MbedTLS sources separately against the guest Picolibc."""
import concurrent.futures
import os
from pathlib import Path
import shlex
import subprocess

root = Path('build/mbedtls/mbedtls-3.6.5')
objects = Path('build/user-tls-objects')
objects.mkdir(exist_ok=True)
cc = shlex.split(os.environ.get('CC', 'gcc'))
flags = shlex.split(os.environ['NOTOS_CFLAGS']) + [
    '-nostdinc', '-isystem', 'build/sysroot/include/notos', '-isystem',
    subprocess.check_output(cc + ['-print-file-name=include'], text=True).strip(),
    '-Iuser/include', '-Iuser/curl_port', '-I' + str(root / 'include'),
    '-DMBEDTLS_CONFIG_FILE="notos_user_mbedtls_config.h"',
    '-U__linux__', '-U__unix__', '-D__NOTOS__',
]

def compile_source(source):
    target = objects / (source.stem + '.o')
    subprocess.run(cc + flags + ['-c', str(source), '-o', str(target)], check=True)
    return str(target)

with concurrent.futures.ThreadPoolExecutor(max_workers=4) as executor:
    paths = list(executor.map(compile_source, sorted((root / 'library').glob('*.c'))))
subprocess.run(['ar', 'rcs', 'build/libuser-mbedtls.a'] + paths, check=True)
