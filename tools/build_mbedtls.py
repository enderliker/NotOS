#!/usr/bin/env python3
"""Build pinned portable TLS 1.2/1.3 without host libc or hardware crypto."""
import concurrent.futures
import os
from pathlib import Path
import shlex
import subprocess
root=Path('build/mbedtls/mbedtls-3.6.5')
objects=Path('build/mbedtls-objects');objects.mkdir(exist_ok=True)
flags=shlex.split(os.environ['NOTOS_CFLAGS'])+[
    '-ffunction-sections','-fdata-sections','-Ikernel/freestanding','-Ikernel',
    '-I'+str(root/'include'),'-DMBEDTLS_CONFIG_FILE="notos_mbedtls_config.h"',
    '-U__linux__','-U__unix__','-nostdinc','-isystem',
    subprocess.check_output(shlex.split(os.environ.get('CC','gcc'))+['-print-file-name=include'],text=True).strip(),
]
def compile_source(source):
    target=objects/(source.stem+'.o')
    subprocess.run(shlex.split(os.environ.get('CC','gcc'))+flags+['-c',str(source),'-o',str(target)],check=True)
    return str(target)
with concurrent.futures.ThreadPoolExecutor(max_workers=4) as executor:
    paths=list(executor.map(compile_source,sorted((root/'library').glob('*.c'))))
subprocess.run(['ar','rcs','build/libmbedtls.a']+paths,check=True)
print('MbedTLS freestanding objects:',len(paths))
