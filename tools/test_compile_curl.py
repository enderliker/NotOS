#!/usr/bin/env python3
import concurrent.futures
import subprocess
from pathlib import Path

root = Path('build/curl/curl-8.11.1')
inc = subprocess.check_output(['gcc', '-print-file-name=include'], text=True).strip()
flags = [
    '-m32', '-march=i486', '-msoft-float', '-std=c11', '-Os',
    '-ffunction-sections', '-fdata-sections', '-fno-builtin', '-ffreestanding',
    '-fno-pie', '-fno-pic', '-fno-stack-protector', '-fno-asynchronous-unwind-tables',
    '-nostdinc', '-isystem', 'build/sysroot/include/notos', '-isystem', inc,
    '-Iuser/include', '-Iuser/curl_port',
    '-Ibuild/curl/curl-8.11.1/include', '-Ibuild/curl/curl-8.11.1/lib',
    '-DHAVE_CONFIG_H', '-DBUILDING_LIBCURL', '-DCURL_STATICLIB',
    '-DHTTP_ONLY=1', '-D_POSIX_C_SOURCE=200809L', '-D_DEFAULT_SOURCE',
    '-U__linux__', '-U__unix__', '-D__NOTOS__'
]

def check_file(f):
    res = subprocess.run(['gcc'] + flags + ['-c', str(f), '-o', '/dev/null'], capture_output=True, text=True)
    return f.name, res.returncode == 0, res.stderr

sources = sorted((root / 'lib').glob('*.c'))
with concurrent.futures.ThreadPoolExecutor(max_workers=8) as ex:
    results = list(ex.map(check_file, sources))

failed = [(name, err) for name, ok, err in results if not ok]
ok = len(results) - len(failed)
print(f"lib/*.c results -> OK: {ok}, Failed: {len(failed)}", flush=True)
for name, err in failed:
    print(f"--- {name} ---", flush=True)
    print(err[:400], flush=True)
