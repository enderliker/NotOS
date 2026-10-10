#!/usr/bin/env python3
"""Fetch a SHA256-pinned picolibc dependency for the build host, never for the guest."""
import hashlib
import os
from pathlib import Path
import shutil
import tarfile
import urllib.request

SHA256 = 'b4671ddeecbe427b77fa9a863be92c74a9059e0373a5b31343c791ff2337db64'
URL = 'https://github.com/picolibc/picolibc/releases/download/1.8.11/picolibc-1.8.11.tar.xz'
archive = Path(os.environ.get('PICOLIBC_ARCHIVE', '.cache/picolibc-1.8.11.tar.xz'))
if not archive.exists():
    archive.parent.mkdir(parents=True, exist_ok=True)
    temporary = archive.with_suffix('.download')
    try:
        with urllib.request.urlopen(URL, timeout=40) as source:
            temporary.write_bytes(source.read(8 * 1024 * 1024))
        if hashlib.sha256(temporary.read_bytes()).hexdigest() != SHA256:
            raise RuntimeError('Picolibc checksum mismatch')
        temporary.replace(archive)
    finally:
        temporary.unlink(missing_ok=True)
if hashlib.sha256(archive.read_bytes()).hexdigest() != SHA256:
    raise RuntimeError('Picolibc checksum mismatch; refusing to build')
destination = Path('build/picolibc')
if destination.exists():
    shutil.rmtree(destination)
destination.mkdir(parents=True)
with tarfile.open(archive) as source:
    source.extractall(destination, filter='data')
Path('build/picolibc.ready').write_text(SHA256 + '\n')
