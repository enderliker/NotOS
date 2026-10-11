#!/usr/bin/env python3
"""Cross-build freestanding curl 8.11.1 for NotOS userspace."""
import concurrent.futures
import os
import shlex
from pathlib import Path
import subprocess

root = Path('build/curl/curl-8.11.1')
lib_root = root / 'lib'
src_root = root / 'src'
cc = shlex.split(os.environ.get('CC', 'gcc'))

inc = subprocess.check_output(cc + ['-print-file-name=include'], text=True).strip()

base_flags = [
    '-m32', '-march=i486', '-mno-sse', '-mno-sse2', '-mno-mmx', '-msoft-float', '-std=c11', '-Os',
    '-ffunction-sections', '-fdata-sections', '-fno-builtin', '-ffreestanding',
    '-fno-pie', '-fno-pic', '-fno-stack-protector', '-fno-asynchronous-unwind-tables',
    '-Wall', '-Wextra',
    '-nostdinc', '-isystem', 'build/sysroot/include/notos', '-isystem', inc,
    '-Iuser/include', '-Iuser/curl_port',
    '-I' + str(root / 'include'), '-I' + str(lib_root),
    '-DHAVE_CONFIG_H', '-DCURL_STATICLIB',
    '-DHTTP_ONLY=1', '-D_POSIX_C_SOURCE=200809L', '-D_DEFAULT_SOURCE',
    '-U__linux__', '-U__unix__', '-D__NOTOS__'
]

obj_dir = Path('build/curl-objects')
obj_dir.mkdir(exist_ok=True, parents=True)

lib_flags = base_flags + ['-DBUILDING_LIBCURL']
tool_flags = base_flags + ['-I' + str(src_root)]

lib_sources = sorted(lib_root.glob('*.c')) + [
    lib_root / 'vauth/vauth.c',
    lib_root / 'vauth/digest.c',
    lib_root / 'vtls/vtls.c',
    lib_root / 'vquic/vquic.c',
]

def compile_lib(source):
    target = obj_dir / ('lib_' + source.stem + '.o')
    subprocess.run(cc + lib_flags + ['-c', str(source), '-o', str(target)], check=True)
    return str(target)

tool_sources = sorted(src_root.glob('*.c'))
curlx_sources = [
    lib_root / 'dynbuf.c',
    lib_root / 'base64.c',
    lib_root / 'nonblock.c',
    lib_root / 'strtoofft.c',
    lib_root / 'warnless.c',
    lib_root / 'timediff.c',
]

def compile_tool(source):
    prefix = 'curlx_' if source in curlx_sources else 'tool_'
    target = obj_dir / (prefix + source.stem + '.o')
    subprocess.run(cc + tool_flags + ['-c', str(source), '-o', str(target)], check=True)
    return str(target)

workers = min(os.cpu_count() or 1, 8)
with concurrent.futures.ThreadPoolExecutor(max_workers=workers) as ex:
    lib_objs = list(ex.map(compile_lib, lib_sources))
    tool_objs = list(ex.map(compile_tool, tool_sources + curlx_sources))

subprocess.run(['ar', 'rcs', 'build/libcurl.a'] + lib_objs, check=True)
subprocess.run(['ar', 'rcs', 'build/libcurltool.a'] + tool_objs, check=True)
print(f"curl objects built: {len(lib_objs)} libcurl, {len(tool_objs)} tool")
