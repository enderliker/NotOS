#!/usr/bin/env python3
"""Embed full DER roots for chain verification, without private keys."""
import base64
from pathlib import Path
import re
import sys
certificates=[base64.b64decode(p) for p in re.findall(rb'-----BEGIN CERTIFICATE-----\s*(.*?)-----END CERTIFICATE-----',Path(sys.argv[1]).read_bytes(),re.S)]
if not certificates:raise ValueError('empty CA bundle')
data=b''.join(certificates)
lines=['/* Generated from the reviewed PEM CA snapshot. */','static const unsigned char trust_der[]={']
for i in range(0,len(data),16):lines.append(','.join('0x%02x'%b for b in data[i:i+16])+',')
lines+=['};','static const unsigned trust_lengths[]={'+','.join(str(len(c)) for c in certificates)+'};']
Path(sys.argv[2]).write_text('\n'.join(lines)+'\n')
print('DER anchors:',len(certificates),'bytes:',len(data))
