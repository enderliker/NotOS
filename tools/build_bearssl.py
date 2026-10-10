#!/usr/bin/env python3
"""Build portable i486 objects; disable OS RNG, OS time and CPU extensions."""
import concurrent.futures
import os
from pathlib import Path
import shlex
import subprocess

root = Path('build/bearssl/bearssl-0.6')
objects = Path('build/bearssl-objects')
objects.mkdir(exist_ok=True)
flags = shlex.split(os.environ['NOTOS_CFLAGS']) + [
    '-ffunction-sections', '-fdata-sections', '-Ikernel/freestanding',
    '-I' + str(root / 'inc'), '-I' + str(root / 'src'),
    '-DBR_64=0', '-DBR_INT128=0', '-DBR_AES_X86NI=0', '-DBR_SSE2=0',
    '-DBR_RDRAND=0', '-DBR_USE_URANDOM=0', '-DBR_USE_WIN32_RAND=0',
    '-DBR_USE_UNIX_TIME=0', '-DBR_USE_WIN32_TIME=0', '-DBR_POWER8=0',
]
def compile_source(source):
    target = objects / (source.relative_to(root / 'src').as_posix().replace('/', '_') + '.o')
    subprocess.run(shlex.split(os.environ.get('CC', 'gcc')) + flags + ['-c', str(source), '-o', str(target)], check=True)
    return str(target)
with concurrent.futures.ThreadPoolExecutor(max_workers=min(os.cpu_count() or 1, 4)) as executor:
    sources = sorted((root / 'src').rglob('*.c'))
    sources = [p for p in sources if not ('#define BR_ENABLE_INTRINSICS' in p.read_text())]
    paths = list(executor.map(compile_source, sources))
subprocess.run(['ar', 'rcs', 'build/libbearssl.a'] + paths, check=True)
print('BearSSL portable objects:', len(paths))
