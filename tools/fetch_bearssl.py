#!/usr/bin/env python3
"""Fetch a SHA256-pinned MIT dependency for the build host, never for the guest."""
import hashlib
import os
from pathlib import Path
import shutil
import tarfile
import urllib.request

SHA256 = '6705bba1714961b41a728dfc5debbe348d2966c117649392f8c8139efc83ff14'
URL = 'https://bearssl.org/bearssl-0.6.tar.gz'
archive = Path(os.environ.get('BEARSSL_ARCHIVE', '.cache/bearssl-0.6.tar.gz'))
if not archive.exists():
    archive.parent.mkdir(parents=True, exist_ok=True)
    temporary = archive.with_suffix('.download')
    try:
        with urllib.request.urlopen(URL, timeout=40) as source:
            temporary.write_bytes(source.read(2 * 1024 * 1024))
        if hashlib.sha256(temporary.read_bytes()).hexdigest() != SHA256:
            raise RuntimeError('BearSSL checksum mismatch')
        temporary.replace(archive)
    finally:
        temporary.unlink(missing_ok=True)
if hashlib.sha256(archive.read_bytes()).hexdigest() != SHA256:
    raise RuntimeError('BearSSL checksum mismatch; refusing to build')
destination = Path('build/bearssl')
if destination.exists():
    shutil.rmtree(destination)
destination.mkdir(parents=True)
with tarfile.open(archive) as source:
    source.extractall(destination, filter='data')
Path('build/bearssl.ready').write_text(SHA256 + '\n')
