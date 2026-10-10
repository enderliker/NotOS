#!/usr/bin/env python3
"""Cross-build an OS-independent libc; target executables never run on the host."""
import json
import os
from pathlib import Path
import shlex
import shutil
import subprocess
root=Path('build/picolibc/picolibc-1.8.11').resolve()
build=Path('build/picolibc-build').resolve()
install=Path('build/sysroot').resolve()
cc=shlex.split(os.environ.get('CC','gcc'))
flags=shlex.split(os.environ['NOTOS_CFLAGS'])
flags=[f for f in flags if f!='-Werror']
compiler_headers=subprocess.check_output(cc+['-print-file-name=include'],text=True).strip()
flags+=['-nostdinc','-isystem',compiler_headers,'-U__linux__','-U__unix__','-D__NOTOS__']
cross=Path('build/picolibc-cross.ini').resolve()
cross.write_text("[binaries]\nc = "+repr(cc+flags)+"\nar = 'ar'\nstrip = 'strip'\n"+
    "[host_machine]\nsystem = 'none'\ncpu_family = 'x86'\ncpu = 'i486'\nendian = 'little'\n"+
    "[properties]\nneeds_exe_wrapper = true\n")
options=['-Dmultilib=false','-Dtests=false','-Dpicocrt=false','-Dpicocrt-lib=false',
    '-Dsemihost=false','-Dsingle-thread=true','-Dthread-local-storage=false',
    '-Dthread-local-storage-api=false','-Dnewlib-global-errno=true','-Dinitfini-array=false',
    '-Dinitfini=false','-Dposix-console=true','-Dformat-default=long-long',
    '-Dio-long-long=true','-Dprintf-aliases=false','-Dfreestanding=true',
    '-Dbuild-type-subdir=notos']
signature=json.dumps({'cc':cc,'flags':flags,'options':options,
    'source':Path('build/picolibc.ready').read_text()},sort_keys=True)
stamp=build/'.notos-config'
if build.exists() and (not stamp.exists() or stamp.read_text()!=signature):
    shutil.rmtree(build)
    if install.exists():shutil.rmtree(install)
if not build.exists():
    subprocess.run(['meson','setup',str(build),str(root),'--cross-file',str(cross),
        '--prefix',str(install),'--libdir','lib','--buildtype','minsize']+options,check=True)
stamp.write_text(signature)
subprocess.run(['meson','compile','-C',str(build),'-j','4'],check=True)
subprocess.run(['meson','install','-C',str(build),'--no-rebuild'],check=True)
Path('build/picolibc-built.ready').write_text('picolibc 1.8.11, freestanding i386, no host libc or semihosting\n')
