#!/usr/bin/env python3
"""Reproducible PEM -> BearSSL anchors using Python's standard library DER reader."""
import base64
from pathlib import Path
import re
import sys

def element(data, offset=0):
    tag = data[offset]; cursor = offset + 1; size = data[cursor]; cursor += 1
    if size & 128:
        count = size & 127
        if not 0 < count <= 4: raise ValueError('invalid DER length')
        size = int.from_bytes(data[cursor:cursor+count], 'big'); cursor += count
    end = cursor + size
    if end > len(data): raise ValueError('truncated DER')
    return tag, data[cursor:end], end, data[offset:end]
def children(data):
    result = []; cursor = 0
    while cursor < len(data):
        item = element(data, cursor); result.append(item); cursor = item[2]
    return result

def anchors(pem):
    for encoded in re.findall(rb'-----BEGIN CERTIFICATE-----\s*(.*?)-----END CERTIFICATE-----', pem, re.S):
        der = base64.b64decode(encoded)
        cert = children(element(der)[1]); tbs = children(cert[0][1])
        start = 1 if tbs[0][0] == 0xa0 else 0
        subject = tbs[start+4][3]; spki = children(tbs[start+5][1])
        algorithm = children(spki[0][1]); oid = algorithm[0][1].hex()
        bits = spki[1][1]
        if bits[0] != 0: raise ValueError('invalid SPKI bit string')
        if oid == '2a864886f70d010101':
            key = children(element(bits[1:])[1])
            yield subject, ('rsa', key[0][1].lstrip(b'\0'), key[1][1].lstrip(b'\0'))
        elif oid == '2a8648ce3d0201':
            curve = {'2a8648ce3d030107':23, '2b81040022':24, '2b81040023':25}[algorithm[1][1].hex()]
            yield subject, ('ec', curve, bits[1:])
        else: raise ValueError('unsupported root public key')

source, destination = map(Path, sys.argv[1:3])
items = list(anchors(source.read_bytes()))
if not items: raise ValueError('empty trust store')
output = ['/* Generated from a committed PEM snapshot; public keys, no secrets. */']
def array(name, data):
    output.append('static const unsigned char ' + name + '[] = {' + ','.join('0x%02x' % b for b in data) + '};')
for i, (dn, key) in enumerate(items):
    array(f'ta_dn_{i}', dn)
    if key[0] == 'rsa': array(f'ta_n_{i}', key[1]); array(f'ta_e_{i}', key[2])
    else: array(f'ta_q_{i}', key[2])
output.append('static const br_x509_trust_anchor trust_anchors[] = {')
for i, (dn, key) in enumerate(items):
    if key[0] == 'rsa':
        value = '{BR_KEYTYPE_RSA, {.rsa={(unsigned char *)ta_n_%d,sizeof(ta_n_%d),(unsigned char *)ta_e_%d,sizeof(ta_e_%d)}}}' % (i,i,i,i)
    else:
        value = '{BR_KEYTYPE_EC, {.ec={%d,(unsigned char *)ta_q_%d,sizeof(ta_q_%d)}}}' % (key[1],i,i)
    output.append('{{(unsigned char *)ta_dn_%d,sizeof(ta_dn_%d)},BR_X509_TA_CA,%s},' % (i,i,value))
output.append('};')
destination.write_text('\n'.join(output) + '\n')
print('Trust anchors:', len(items))
