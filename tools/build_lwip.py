#!/usr/bin/env python3
"""Cross-build unchanged lwIP raw API; NotOS owns its syscall/scheduling adapters."""
import concurrent.futures
import os
from pathlib import Path
import shlex
import subprocess
root=Path('build/lwip/lwip-STABLE-2_2_1_RELEASE/src')
output=Path(os.environ.get('LWIP_OUTPUT','build/liblwip.a'))
objects=output.with_suffix('');objects.mkdir(exist_ok=True)
cc=shlex.split(os.environ.get('CC','gcc'))
flags=shlex.split(os.environ['NOTOS_CFLAGS'])+['-Ikernel','-Ikernel/freestanding','-Ikernel/lwip_port','-I'+str(root/'include'),'-nostdinc','-isystem',subprocess.check_output(cc+['-print-file-name=include'],text=True).strip(),'-U__linux__','-U__unix__']+shlex.split(os.environ.get('LWIP_EXTRA_FLAGS',''))
sources=list((root/'core').glob('*.c'))+list((root/'core/ipv4').glob('*.c'))+[root/'netif/ethernet.c']
def build(source):
    target=objects/(source.stem+'.o')
    subprocess.run(cc+flags+['-c',str(source),'-o',str(target)],check=True)
    return str(target)
with concurrent.futures.ThreadPoolExecutor(max_workers=4) as executor:paths=list(executor.map(build,sorted(sources)))
subprocess.run(['ar','rcs',str(output)]+paths,check=True)
print('lwIP freestanding objects:',len(paths))
