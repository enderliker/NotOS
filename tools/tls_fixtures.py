#!/usr/bin/env python3
"""Generate disposable private keys/test CA only under build; never production trust."""
from pathlib import Path
import subprocess

root=Path('build/tls-fixtures');root.mkdir(parents=True,exist_ok=True)
def openssl(*args):
    subprocess.run(['openssl',*args],check=True,stdout=subprocess.DEVNULL,stderr=subprocess.DEVNULL)
openssl('req','-x509','-newkey','rsa:2048','-nodes','-keyout',str(root/'ca.key'),
    '-out',str(root/'ca.pem'),'-days','7','-sha256','-subj','/CN=NotOS ephemeral test CA',
    '-addext','basicConstraints=critical,CA:TRUE','-addext','keyUsage=critical,keyCertSign,cRLSign')
def leaf(name,san,eku='serverAuth',bits=2048):
    openssl('req','-new','-newkey',f'rsa:{bits}','-nodes','-keyout',str(root/(name+'.key')),
        '-out',str(root/(name+'.csr')),'-subj','/CN=localhost')
    extensions='basicConstraints=critical,CA:FALSE\nkeyUsage=critical,digitalSignature\nextendedKeyUsage='+eku+'\n'
    if san: extensions+='subjectAltName='+san+'\n'
    (root/(name+'.ext')).write_text(extensions)
    openssl('x509','-req','-in',str(root/(name+'.csr')),'-CA',str(root/'ca.pem'),'-CAkey',str(root/'ca.key'),
        '-CAcreateserial','-out',str(root/(name+'.pem')),'-days','2','-sha256','-extfile',str(root/(name+'.ext')))
    openssl('x509','-in',str(root/(name+'.pem')),'-outform','DER','-out',str(root/(name+'.der')))
leaf('valid','DNS:localhost,DNS:*.notos.test')
leaf('san-mismatch','DNS:other.test')
leaf('no-san',None)
leaf('client-only','DNS:localhost','clientAuth')
leaf('weak','DNS:localhost',bits=1024)
leaf('weak-edge','DNS:localhost',bits=2047)

# A restricted intermediate must not confer serverAuth through its leaf.
openssl('req','-new','-newkey','rsa:2048','-nodes','-keyout',str(root/'intermediate.key'),
    '-out',str(root/'intermediate.csr'),'-subj','/CN=Client-only intermediate')
(root/'intermediate.ext').write_text('basicConstraints=critical,CA:TRUE,pathlen:0\nkeyUsage=critical,keyCertSign,cRLSign\nextendedKeyUsage=clientAuth\n')
openssl('x509','-req','-in',str(root/'intermediate.csr'),'-CA',str(root/'ca.pem'),'-CAkey',str(root/'ca.key'),
    '-CAcreateserial','-out',str(root/'intermediate.pem'),'-days','2','-sha256','-extfile',str(root/'intermediate.ext'))
openssl('x509','-req','-in',str(root/'valid.csr'),'-CA',str(root/'intermediate.pem'),'-CAkey',str(root/'intermediate.key'),
    '-CAcreateserial','-out',str(root/'restricted-leaf.pem'),'-days','2','-sha256','-extfile',str(root/'valid.ext'))
(root/'restricted-chain.pem').write_bytes((root/'restricted-leaf.pem').read_bytes()+(root/'intermediate.pem').read_bytes())
(root/'restricted-chain.key').write_bytes((root/'valid.key').read_bytes())
for key in root.glob('*.key'):key.chmod(0o600)
Path('build/tls-fixtures.ready').write_text('ephemeral, never a production trust anchor\n')
