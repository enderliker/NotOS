#!/usr/bin/env python3
"""Exercise upstream curl over the guest TCP stack and filesystem."""
import re
import socketserver
import threading
import time
from pathlib import Path
from harness import Machine
from socket_fixture import BODY, DNS


class HTTPPeer(socketserver.ThreadingTCPServer):
    allow_reuse_address = True
    daemon_threads = True

    def __init__(self):
        self.requests = []
        self.errors = []
        self.cancelling = False
        super().__init__(('127.0.0.1', 0), HTTPHandler)
        self.thread = threading.Thread(target=self.serve_forever, daemon=True)
        self.thread.start()

    @property
    def port(self):
        return self.server_address[1]

    def __enter__(self):
        return self

    def __exit__(self, *args):
        self.shutdown()
        self.server_close()
        self.thread.join()
        assert not self.errors, self.errors


class HTTPHandler(socketserver.BaseRequestHandler):
    def handle(self):
        self.request.settimeout(10)
        try:
            request = b''
            while b'\r\n\r\n' not in request:
                chunk = self.request.recv(4096)
                if not chunk and self.server.cancelling:
                    return
                assert chunk, 'request closed before headers'
                request += chunk
            headers, body = request.split(b'\r\n\r\n', 1)
            first_line = headers.split(b'\r\n')[0]
            self.server.requests.append(first_line)
            length = re.search(rb'\r\nContent-Length: (\d+)', headers, re.I)
            expected = int(length[1]) if length else 0
            while len(body) < expected:
                chunk = self.request.recv(4096)
                assert chunk, 'request closed before body'
                body += chunk
            path = first_line.split()[1]
            assert b'User-Agent: curl/8.11.1' in headers, headers
            if path == b'/slow':
                time.sleep(2)
                return
            if path == b'/upload':
                assert first_line.startswith(b'POST ') and body == BODY, len(body)
                payload = b'upload-verified\n'
            else:
                payload = BODY if path == b'/binary' else b'curl-guest-response\n'
            status = b'404 Not Found' if path == b'/missing' else b'200 OK'
            response = b'HTTP/1.1 ' + status + b'\r\nConnection: close\r\nContent-Length: '
            response += str(len(payload)).encode() + b'\r\n\r\n' + payload
            for offset in range(0, len(response), 137):
                self.request.sendall(response[offset:offset + 137])
        except Exception as error:
            self.server.errors.append(repr(error))


def snapshot(machine, files=0):
    frames = re.search(rb'free_frames=(\d+)', machine.command('mem', '\nnotos> '))[1]
    heap = re.search(rb'HEAP: [^\n]+\n', machine.command('heap', '\nnotos> '))[0]
    reply = machine.command('sockets', '\nnotos> ')
    assert b'open=0 queries=0' in reply, reply
    reply = machine.command('vfs', '\nnotos> ')
    assert f'files={files} nodes={files}'.encode() in reply and b'descriptions=0' in reply, reply
    return frames, heap


def launch(machine, arguments):
    start = len(machine.output)
    reply = machine.command('curl' + (' ' + arguments if arguments is not None else ''), 'notos> ')
    match = re.search(rb'CURL: started pid=(\d+)', reply)
    assert match, reply
    return int(match[1]), start


def finish(machine, pid, start, status=0):
    marker = f'PROCESS: pid={pid} exit='
    machine.expect(marker, start, timeout=20)
    position = machine.output.index(marker.encode(), start)
    machine.expect('\n', position)
    reply = bytes(machine.output[start:])
    assert re.search(rb'PROCESS: pid=' + str(pid).encode() + rb' exit=(\d+)', reply)[1] == str(status).encode(), reply
    assert b'PROCESS FAULT:' not in reply and b'cleanup failed' not in reply, reply
    return reply


def run(machine, arguments, status=0):
    return finish(machine, *launch(machine, arguments), status)


def check_binary(machine):
    start = len(machine.output)
    reply = machine.command('libc-run curl-file', 'notos> ')
    pid = int(re.search(rb'LIBC: started pid=(\d+)', reply)[1])
    reply = finish(machine, pid, start)
    assert b'LIBC: curl binary file/content/unlink PASS' in reply, reply


def main():
    link_map = Path('build/curl.map').read_text()
    assert 'build/sysroot/lib/notos/libc.a' in link_map
    assert all(text not in link_map.lower() for text in ('/usr/lib/', 'libgcc', 'semihost'))
    for memory in ('16M', '64M'):
        with HTTPPeer() as peer, Machine(network=True, memory=memory, logname='curl-' + memory) as machine:
            baseline = snapshot(machine)
            reply = run(machine, '--version')
            assert b'curl 8.11.1 (notos) libcurl/8.11.1' in reply and b'Protocols: http\n' in reply, reply
            run(machine, None, 2)
            run(machine, '   ', 2)
            run(machine, '--unsupported-option', 2)
            run(machine, '-sS https://10.0.2.2/', 1)
            run(machine, '  --version  ')
            run(machine, '--version' + ' ' * (256 - len('--version')))
            run(machine, '--version ' + ' '.join(['x'] * 14))
            for text in ('--version ' + ' '.join(['x'] * 15), 'x' * 257):
                reply = machine.command('curl ' + text, '\nnotos> ')
                assert b'CURL: started' not in reply, reply
                assert b'CURL ERROR:' in reply or b'input too long; discarded' in reply, reply
            assert snapshot(machine) == baseline
            base = f'http://10.0.2.2:{peer.port}'
            assert b'curl-guest-response\n' in run(machine, '-sS ' + base + '/text')
            run(machine, '-sS -o /tmp/curl.bin ' + base + '/binary')
            assert snapshot(machine, files=1)[1] == baseline[1]
            assert b'upload-verified\n' in run(machine, '-sS --data-binary @/tmp/curl.bin ' + base + '/upload')
            check_binary(machine)
            assert snapshot(machine) == baseline
            run(machine, '-sS -f ' + base + '/missing', 22)
            pid, start = launch(machine, '-sS --max-time 1 ' + base + '/slow')
            machine.command('echo curl-responsive', '\ncurl-responsive\nnotos> ')
            machine.command('int3', 'IDT: breakpoint OK; registers and flags preserved\nnotos> ')
            finish(machine, pid, start, 28)
            assert snapshot(machine) == baseline
            expected_requests = len(peer.requests) + 4
            waiting = [launch(machine, '-sS --max-time 5 ' + base + '/slow') for _ in range(4)]
            deadline = time.monotonic() + 5
            while len(peer.requests) < expected_requests:
                assert time.monotonic() < deadline, peer.requests
                time.sleep(.01)
            reply = machine.command('curl --version', '\nnotos> ')
            assert b'CURL ERROR:' in reply and b'CURL: started' not in reply, reply
            peer.cancelling = True
            for pid, start in waiting:
                machine.command(f'kill {pid}', '\nnotos> ')
                finish(machine, pid, start, 143)
            assert snapshot(machine) == baseline
            run(machine, '--version')
        with Machine(network=True, memory=memory, logname='curl-refused-' + memory) as machine:
            baseline = snapshot(machine)
            run(machine, f'-sS http://10.0.2.2:{peer.port}/', 7)
            assert snapshot(machine) == baseline
        print(f'PASS: curl HTTP-only, arguments, binary tmpfs/upload, timeout/errors and cleanup on {memory}')
    with DNS() as dns, HTTPPeer() as peer, Machine(network=True, image='build/notos-socket-test.img', logname='curl-dns') as machine:
        machine.command('net-config 10.0.2.15 10.0.2.2 10.0.2.2', 'configuration applied\nnotos> ')
        baseline = snapshot(machine)
        assert b'curl-guest-response\n' in run(machine, f'-sS http://curl.socket.test:{peer.port}/text')
        run(machine, '-sS http://missing.socket.test/', 6)
        assert 'curl.socket.test' in dns.names and 'missing.socket.test' in dns.names, dns.names
        assert snapshot(machine) == baseline
    print('PASS: curl guest DNS resolution/NXDOMAIN and static upstream link')


if __name__ == '__main__':
    main()
