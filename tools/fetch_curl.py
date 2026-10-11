#!/usr/bin/env python3
"""Fetch a SHA256-pinned curl dependency for the build host, never for the guest."""
import hashlib
import os
from pathlib import Path
import shutil
import tarfile
import urllib.request

SHA256 = 'c7ca7db48b0909743eaef34250da02c19bc61d4f1dcedd6603f109409536ab56'
URL = 'https://curl.se/download/curl-8.11.1.tar.xz'
archive = Path(os.environ.get('CURL_ARCHIVE', '.cache/curl-8.11.1.tar.xz'))
if not archive.exists():
    archive.parent.mkdir(parents=True, exist_ok=True)
    temporary = archive.with_suffix('.download')
    try:
        with urllib.request.urlopen(URL, timeout=40) as source:
            temporary.write_bytes(source.read(8 * 1024 * 1024))
        if hashlib.sha256(temporary.read_bytes()).hexdigest() != SHA256:
            raise RuntimeError('Curl checksum mismatch')
        temporary.replace(archive)
    finally:
        temporary.unlink(missing_ok=True)
if hashlib.sha256(archive.read_bytes()).hexdigest() != SHA256:
    raise RuntimeError('Curl checksum mismatch; refusing to build')
destination = Path('build/curl')
if destination.exists():
    shutil.rmtree(destination)
destination.mkdir(parents=True)
with tarfile.open(archive) as source:
    source.extractall(destination, filter='data')
Path('build/curl.ready').write_text(SHA256 + '\n')
