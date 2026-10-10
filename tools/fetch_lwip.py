#!/usr/bin/env python3
"""Fetch a SHA256-pinned lwip dependency for the build host, never for the guest."""
import hashlib
import os
from pathlib import Path
import shutil
import tarfile
import urllib.request

SHA256 = 'ce0b7461c0ad9602c376f0bf07c5eb7253b48c7bf66f011c6bf3e2a96731c539'
URL = 'https://codeload.github.com/lwip-tcpip/lwip/tar.gz/refs/tags/STABLE-2_2_1_RELEASE'
archive = Path(os.environ.get('LWIP_ARCHIVE', '.cache/lwip-2.2.1.tar.gz'))
if not archive.exists():
    archive.parent.mkdir(parents=True, exist_ok=True)
    temporary = archive.with_suffix('.download')
    try:
        with urllib.request.urlopen(URL, timeout=40) as source:
            temporary.write_bytes(source.read(8 * 1024 * 1024))
        if hashlib.sha256(temporary.read_bytes()).hexdigest() != SHA256:
            raise RuntimeError('lwIP checksum mismatch')
        temporary.replace(archive)
    finally:
        temporary.unlink(missing_ok=True)
if hashlib.sha256(archive.read_bytes()).hexdigest() != SHA256:
    raise RuntimeError('lwIP checksum mismatch; refusing to build')
destination = Path('build/lwip')
if destination.exists():
    shutil.rmtree(destination)
destination.mkdir(parents=True)
with tarfile.open(archive) as source:
    source.extractall(destination, filter='data')
Path('build/lwip.ready').write_text(SHA256 + '\n')
