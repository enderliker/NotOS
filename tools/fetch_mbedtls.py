#!/usr/bin/env python3
"""Fetch a SHA256-pinned Apache-2.0 dependency for the build host, never for the guest."""
import hashlib
import os
from pathlib import Path
import shutil
import tarfile
import urllib.request

SHA256 = '4a11f1777bb95bf4ad96721cac945a26e04bf19f57d905f241fe77ebeddf46d8'
URL = 'https://github.com/Mbed-TLS/mbedtls/releases/download/mbedtls-3.6.5/mbedtls-3.6.5.tar.bz2'
archive = Path(os.environ.get('MBEDTLS_ARCHIVE', '.cache/mbedtls-3.6.5.tar.bz2'))
if not archive.exists():
    archive.parent.mkdir(parents=True, exist_ok=True)
    temporary = archive.with_suffix('.download')
    try:
        with urllib.request.urlopen(URL, timeout=40) as source:
            temporary.write_bytes(source.read(8 * 1024 * 1024))
        if hashlib.sha256(temporary.read_bytes()).hexdigest() != SHA256:
            raise RuntimeError('MbedTLS checksum mismatch')
        temporary.replace(archive)
    finally:
        temporary.unlink(missing_ok=True)
if hashlib.sha256(archive.read_bytes()).hexdigest() != SHA256:
    raise RuntimeError('MbedTLS checksum mismatch; refusing to build')
destination = Path('build/mbedtls')
if destination.exists():
    shutil.rmtree(destination)
destination.mkdir(parents=True)
with tarfile.open(archive) as source:
    source.extractall(destination, filter='data')
Path('build/mbedtls.ready').write_text(SHA256 + '\n')
