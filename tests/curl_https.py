#!/usr/bin/env python3
"""Guest curl TLS over local fixtures, using an explicitly downloaded test CA."""
import re
import socketserver
import ssl
import threading
from pathlib import Path
from harness import Machine
from socket_fixture import DNS
from curl import run, snapshot
FIXTURES=Path('build/tls-fixtures')
PAYLOAD=b'curl TLS authenticated guest payload\n'*200
class Peer(socketserver.ThreadingTCPServer):
    allow_reuse_address=True
    daemon_threads=True
    def __init__(self,certificate='valid',tls13=False,truncate=False):
        self.requests=[];self.names=[];self.errors=[];self.truncate=truncate
        self.context=ssl.SSLContext(ssl.PROTOCOL_TLS_SERVER)
        self.context.minimum_version=self.context.maximum_version=ssl.TLSVersion.TLSv1_3 if tls13 else ssl.TLSVersion.TLSv1_2
        self.context.set_ciphers('ECDHE-RSA-AES128-GCM-SHA256:@SECLEVEL=0')
        self.context.load_cert_chain(FIXTURES/(certificate+'.pem'),FIXTURES/(certificate+'.key'))
        self.context.set_servername_callback(lambda sock,name,context:self.names.append(name))
        super().__init__(('127.0.0.1',0),Handler)
    def __enter__(self):
        self.thread=threading.Thread(target=self.serve_forever,daemon=True);self.thread.start();return self
    def __exit__(self,*args):
        self.shutdown();self.server_close();self.thread.join()
class Handler(socketserver.BaseRequestHandler):
    def handle(self):
        try:
            with self.server.context.wrap_socket(self.request,server_side=True) as connection:
                connection.settimeout(10)
                request=b''
                while b'\r\n\r\n' not in request:
                    block=connection.recv(4096)
                    if not block:return
                    request+=block
                self.server.requests.append(request)
                response=b'HTTP/1.0 200 OK\r\nConnection: close\r\n\r\n'+PAYLOAD
                connection.sendall(response)
                if not self.server.truncate:
                    connection.unwrap().close()
        except (OSError,ssl.SSLError) as error:self.server.errors.append(type(error).__name__)
class CAPeer(socketserver.ThreadingTCPServer):
    allow_reuse_address=True
    daemon_threads=True
    def __enter__(self):
        self.thread=threading.Thread(target=self.serve_forever,daemon=True);self.thread.start();return self
    def __exit__(self,*args):self.shutdown();self.server_close();self.thread.join()
class CAHandler(socketserver.BaseRequestHandler):
    def handle(self):
        self.request.recv(4096)
        body=(FIXTURES/'ca.pem').read_bytes()
        self.request.sendall(b'HTTP/1.0 200 OK\r\nContent-Length: '+str(len(body)).encode()+b'\r\n\r\n'+body)
for memory in ('16M','64M'):
    with DNS(), CAPeer(('127.0.0.1',0),CAHandler) as ca, Machine(network=True,memory=memory,image='build/notos-socket-test.img',logname='curl-https-'+memory) as machine:
        machine.command('process-trace on','PROCESS: trace on\nnotos> ')
        machine.command('net-config 10.0.2.15 10.0.2.2 10.0.2.2','configuration applied\nnotos> ')
        baseline=snapshot(machine)
        run(machine,f'-sS -o /tmp/ca.pem http://10.0.2.2:{ca.server_address[1]}/ca')
        for tls13 in (False,True):
            with Peer(tls13=tls13) as peer:
                url=f'https://www.notos.test:{peer.server_address[1]}/'
                run(machine,'-sS '+url,60)
                run(machine,'-ksS '+url,60)
                reply=run(machine,'-sS --cacert /tmp/ca.pem '+url)
                assert PAYLOAD in reply and peer.names[-1]=='www.notos.test' and len(peer.requests)==1,reply
            for certificate in ('san-mismatch','no-san','client-only','weak','weak-edge','restricted-chain','sha1'):
                with Peer(certificate=certificate,tls13=tls13) as peer:
                    run(machine,f'-sS --cacert /tmp/ca.pem https://www.notos.test:{peer.server_address[1]}/',60)
                    assert not peer.requests,(certificate,peer.requests)
            with Peer(tls13=tls13,truncate=True) as peer:
                run(machine,f'-sS --cacert /tmp/ca.pem https://www.notos.test:{peer.server_address[1]}/',56)
        machine.command('rm /tmp/ca.pem','FS: file removed\nnotos> ')
        assert snapshot(machine)==baseline
    print('PASS: curl TLS1.2/TLS1.3, CA/SAN/EKU/key/signature rejection, truncation and cleanup on '+memory)

with DNS(), CAPeer(('127.0.0.1',0),CAHandler) as ca, Peer() as peer, Machine(network=True,image='build/notos-socket-test.img',logname='curl-https-expired',rtc='2050-01-01T00:00:00') as machine:
    machine.command('process-trace on','PROCESS: trace on\nnotos> ')
    machine.command('net-config 10.0.2.15 10.0.2.2 10.0.2.2','configuration applied\nnotos> ')
    baseline=snapshot(machine)
    run(machine,f'-sS -o /tmp/ca.pem http://10.0.2.2:{ca.server_address[1]}/ca')
    run(machine,f'-sS --cacert /tmp/ca.pem https://www.notos.test:{peer.server_address[1]}/',60)
    assert not peer.requests
    machine.command('rm /tmp/ca.pem','FS: file removed\nnotos> ')
    assert snapshot(machine)==baseline
print('PASS: curl TLS expired certificates and cleanup')
with Peer() as peer, Machine(network=True,entropy=False,logname='curl-https-noentropy') as machine:
    machine.command('process-trace on','PROCESS: trace on\nnotos> ')
    baseline=snapshot(machine)
    run(machine,f'-sS https://10.0.2.2:{peer.server_address[1]}/',7)
    assert not peer.requests
    assert snapshot(machine)==baseline
print('PASS: curl HTTPS unavailable without firmware entropy; no connection or resource leak')
