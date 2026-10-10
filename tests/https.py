#!/usr/bin/env python3
"""Real guest TLS against controlled peers. Test CA never enters production image."""
from pathlib import Path
import re
import os
import struct
import socketserver
import ssl
import threading
import time
from harness import Machine

FIXTURES=Path('build/tls-fixtures')
BODY=b'NotOS TLS authenticated payload\n'*200
RESPONSE=b'HTTP/1.0 200 OK\r\nContent-Length: '+str(len(BODY)).encode()+b'\r\nConnection: close\r\n\r\n'+BODY
TEST_IMAGE='build/notos-tls-test.img'

class Peer(socketserver.TCPServer):
    allow_reuse_address=True
    def __init__(self,certificate='valid',cipher='ECDHE-RSA-AES128-GCM-SHA256',truncate=False,tls13=False):
        self.requests=[];self.names=[];self.errors=[];self.truncate=truncate
        self.context=ssl.SSLContext(ssl.PROTOCOL_TLS_SERVER)
        self.context.minimum_version=ssl.TLSVersion.TLSv1_3 if tls13 else ssl.TLSVersion.TLSv1_2
        self.context.maximum_version=self.context.minimum_version
        self.context.set_ciphers(cipher+':@SECLEVEL=0')
        self.context.load_cert_chain(FIXTURES/(certificate+'.pem'),FIXTURES/(certificate+'.key'))
        self.context.set_servername_callback(lambda sock,name,ctx:self.names.append(name))
        super().__init__(('127.0.0.1',0),Handler)
    def __enter__(self):
        self.thread=threading.Thread(target=self.serve_forever,daemon=True);self.thread.start();return self
    def __exit__(self,*args):
        self.shutdown();self.server_close();self.thread.join(timeout=10)
class Handler(socketserver.BaseRequestHandler):
    def handle(self):
        self.request.settimeout(8)
        try:
            with self.server.context.wrap_socket(self.request,server_side=True) as connection:
                request=b''
                while b'\r\n\r\n' not in request:
                    block=connection.recv(1024)
                    if not block:return
                    request+=block
                    assert len(request)<4096
                self.server.requests.append(request)
                connection.sendall(RESPONSE[:9]);time.sleep(.025);connection.sendall(RESPONSE[9:])
                if not self.server.truncate:
                    plain=connection.unwrap();plain.close()  # Authenticated close_notify both ways.
        except (ssl.SSLError,OSError) as error:
            self.server.errors.append(type(error).__name__)

def snapshot(m):
    frames=re.search(rb'free_frames=(\d+)',m.command('mem','\nnotos> '))[1]
    heap=re.search(rb'HEAP: [^\n]+\n',m.command('heap','\nnotos> '))[0]
    return frames,heap

def idle(m):
    for _ in range(20):
        reply=m.command('net','\nnotos> ')
        if b'NET: idle\n' in reply:return
        time.sleep(.02)
    raise AssertionError('TLS transport did not return to idle')

def attempt(m,peer,hostname='localhost',success=True):
    before=snapshot(m)
    target=f'https {hostname}:{peer.server_address[1]} 10.0.2.2'
    start=len(m.output)
    m.command(target,'notos> ')
    m.expect(' bytes; authenticated close_notify\n' if success else 'TLS ERROR:',start,timeout=40)
    idle(m)
    reply=bytes(m.output[start:])
    if success:
        assert f'TLS: verified {hostname}; TLS 1.2\n'.encode() in reply,reply
        assert b'HTTPS: HTTP/1.0 200 OK\n' in reply,reply
        assert f'HTTPS: received {len(RESPONSE)} bytes;'.encode() in reply,reply
    else:
        assert b' bytes; authenticated close_notify\n' not in reply,reply
    assert snapshot(m)==before,reply
    return reply

os.environ['QEMU_PCAP']='1'
with Machine(network=True,image=TEST_IMAGE,logname='https-local') as m:
    with Peer() as peer:
        for _ in range(2):attempt(m,peer)
        attempt(m,peer,hostname='www.notos.test')
        attempt(m,peer,hostname='wrong.test',success=False)
        attempt(m,peer,hostname='a.b.notos.test',success=False)
        assert len(peer.requests)==3 and peer.names[:3]==['localhost','localhost','www.notos.test']
    with Peer() as peer:
        m.command('net-drop response','NET TEST: one-packet loss armed\nnotos> ')
        attempt(m,peer);assert len(peer.requests)==1
    with Peer() as peer:
        m.command('net-drop tamper','NET TEST: one-packet loss armed\nnotos> ')
        reply=attempt(m,peer,success=False)
        assert b'NET TEST: corrupted TLS payload' in reply and b'HTTPS: HTTP/' not in reply,reply
    with Peer(cipher='ECDHE-RSA-CHACHA20-POLY1305') as peer:
        attempt(m,peer);assert len(peer.requests)==1
    for certificate in ('san-mismatch','no-san','client-only','weak','weak-edge','restricted-chain'):
        with Peer(certificate=certificate) as peer:
            attempt(m,peer,success=False);assert not peer.requests,(certificate,peer.requests)
    with Peer(tls13=True) as peer:
        attempt(m,peer,success=False);assert not peer.requests
    with Peer(truncate=True) as peer:
        reply=attempt(m,peer,success=False)
        assert b'truncated TLS connection' in m.output,reply
    with Peer() as peer:
        m.command('alloc 1000000','HEAP: slot=0 bytes=1000000\nnotos> ')
        reply=attempt(m,peer,success=False)
        assert b'TLS ERROR: memory unavailable' in reply and not peer.names and not peer.requests
        m.command('free 0','HEAP: freed slot=0\nnotos> ')
    m.command('https 10.0.2.2' ,'NET ERROR: TLS requires a DNS identity, optionally followed by a connection IP\nnotos> ')
    m.command('heap-test','HEAP: split/coalesce/calloc/OOM selftest PASS\nnotos> ')
    m.command('multitask-run four','SCHED: preemptive multitask/contexts/cleanup PASS\nnotos> ')
    m.command('tls-selftest','TLS: SHA256/profile/RTC selftest PASS\nnotos> ')
    print('PASS: TLS12 AES-GCM/ChaCha20, SNI/SAN/wildcard, repeat, exact plaintext length and clean closure')
    print('PASS: wrong identity, missing SAN, bad leaf/intermediate EKU, weak RSA, TLS13-only and truncation rejected; no leaks')

# Parse a passive Ethernet PCAP: no cleartext HTTP and distinct ClientHello randoms.
os.environ.pop('QEMU_PCAP')
pcap=Path('build/network.pcap').read_bytes();Path('build/https.pcap').write_bytes(pcap)
assert b'GET / HTTP/1.0' not in pcap and BODY[:32] not in pcap
endian='<' if pcap[:4]==bytes.fromhex('d4c3b2a1') else '>'
cursor=24;randoms={}
while cursor+16<=len(pcap):
    size=struct.unpack_from(endian+'IIII',pcap,cursor)[2];cursor+=16
    packet=pcap[cursor:cursor+size];cursor+=size
    if len(packet)<54 or packet[12:14]!=b'\x08\x00' or packet[26:30]!=bytes([10,0,2,15]):continue
    ip_header=(packet[14]&15)*4;tcp_at=14+ip_header
    if len(packet)<tcp_at+20:continue
    tcp_header=(packet[tcp_at+12]>>4)*4;data=packet[tcp_at+tcp_header:]
    if len(data)>43 and data[0]==22 and data[5]==1:
        port=int.from_bytes(packet[tcp_at:tcp_at+2],'big');randoms.setdefault(port,data[11:43])
assert len(randoms)>=5 and len(set(randoms.values()))==len(randoms)
print('PASS: corrupted AEAD ciphertext rejected, RX loss recovered, OOM cleanup and no plaintext in capture')
print('PASS: fresh ClientHello random per attempt; ephemeral test CA isolated from production')

for rtc in ('2000-01-01T00:00:00','2050-01-01T00:00:00'):
    with Peer() as peer,Machine(network=True,image=TEST_IMAGE,rtc=rtc,logname='https-date-'+rtc[:4]) as m:
        attempt(m,peer,success=False);assert not peer.requests
print('PASS: not-yet-valid and expired certificates rejected using RTC validation time')

with Peer() as peer,Machine(network=True,logname='https-untrusted') as m:
    attempt(m,peer,success=False);assert not peer.requests
print('PASS: production image rejects ephemeral test CA')

with Peer() as peer,Machine(network=True,entropy=False,image=TEST_IMAGE,logname='https-no-entropy') as m:
    reply=attempt(m,peer,success=False)
    assert b'fresh firmware entropy required' in reply and not peer.names and not peer.requests
print('PASS: no entropy -> fail closed before connecting or sending application data')
