#!/usr/bin/env python3
from pathlib import Path
boot = Path('build/boot.bin').read_bytes()
kernel = Path('build/kernel.bin').read_bytes()
assert len(boot) == 512 and boot[510:] == b'\x55\xaa'
assert len(kernel) <= 65536, 'Kernel exceeds loader capacity (64 KiB)'
image = bytearray(1024 * 1024)
image[:512] = boot
image[512:512+len(kernel)] = kernel
Path('build/notos.img').write_bytes(image)
print(f'image: {len(image)} bytes; kernel: {len(kernel)} bytes')
