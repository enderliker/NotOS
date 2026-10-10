#!/usr/bin/env python3
"""Offline integration: real guest TCP -> QEMU NAT -> host test server."""
import socketserver
import threading
import time
from harness import Machine
BODY=b'NotOS network test\n' * 400
RESPONSE=(b'HTTP/1.0 200 OK\r\nContent-Type: text/plain\r\nContent-Length: '
          + str(len(BODY)).encode()+b'\r\nConnection: close\r\n\r\n'+BODY)
requests=[]
class Handler(socketserver.BaseRequestHandler):
    def handle(self):
        self.request.settimeout(10)
        request=b''
        while b'\r\n\r\n' not in request:
            part=self.request.recv(2048)
            if not part: return
            request+=part
            assert len(request)<4096
        requests.append(request)
        # Split the HTTP status line and exceed a whole RX ring over time.
        self.request.sendall(RESPONSE[:9])
        time.sleep(.05)
        self.request.sendall(RESPONSE[9:])
with socketserver.TCPServer(('127.0.0.1',0),Handler) as server:
    thread=threading.Thread(target=server.serve_forever,daemon=True)
    thread.start()
    try:
        with Machine(network=True,logname="network") as m:
            assert b'NET: RTL8139 ready; IPv4 10.0.2.15\n' in m.output
            m.command('ring3-test','USER: private-CR3/TSS/syscall/cleanup PASS\nnotos> ')
            m.command('multitask-run fault','SCHED: preemptive multitask/contexts/cleanup PASS\nnotos> ')
            m.p.stdin.write(b'spawn spin\nspawn spin\nspawn spin\nspawn spin\n'); m.p.stdin.flush()
            m.expect('PROCESS: started pid=4')
            port=server.server_address[1]
            for iteration in range(3):
                m.command(f'http 10.0.2.2:{port}', f'HTTP: received {len(RESPONSE)} bytes\n',timeout=20)
                assert b'HTTP: HTTP/1.0 200 OK\n' in m.output
                if iteration==1:
                    m.command('multitask-run budget','SCHED: preemptive multitask/contexts/cleanup PASS\nnotos> ')
                if iteration==0:
                    m.command('user-run io','USER: private-CR3/TSS/syscall/cleanup PASS\nnotos> ')
            m.expect('PROCESS: pid=4 exit=124')
            m.command('net-config 10.0.2.16 10.0.2.2 10.0.2.3','NET: static /24 configuration applied\nnotos> ')
            for loss in ('syn','request','synack','response'):
                m.command('net-drop '+loss,'NET TEST: one-packet loss armed\nnotos> ')
                reply=m.command(f'http 10.0.2.2:{port}',f'HTTP: received {len(RESPONSE)} bytes\n',timeout=25)
                assert f'NET TEST: dropped { {"syn":"SYN","request":"request","synack":"SYN-ACK","response":"response"}[loss]}\n'.encode() in reply,reply
                assert b'HTTP: HTTP/1.0 200 OK\n' in reply,reply
                if loss in ('syn','request'): assert b'TCP: retransmission=1\n' in reply,reply
            m.command('net-config 10.0.2.15 10.0.2.2 10.0.2.3','NET: static /24 configuration applied\nnotos> ')
            assert len(requests)==7, requests
            for request in requests:
                assert request.startswith(b'GET / HTTP/1.0\r\n')
                assert b'User-Agent: NotOS/0.4\r\n' in request
            m.command('net', 'NET: idle\n')
            m.command('http bad..name', 'NET ERROR: empty DNS label\n')
            m.command('http example.com:65536', 'NET ERROR: invalid port\n')
            print('PASS: RTL8139 DMA, ARP, IPv4 and TCP through QEMU NAT')
            print('PASS: seven HTTP responses, packet-loss recovery, static config, split headers and RX ring wrap')
    finally:
        server.shutdown()
        thread.join(timeout=5)
