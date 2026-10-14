"""Boot the actual disk image and interact with the emulated hardware."""
import os
import json
import re
from pathlib import Path
import selectors
import socket
import subprocess
import tempfile
import time

def validate_logname(name):
    """Keep generated basenames compatible with upload-artifact and downloads."""
    if not isinstance(name, str) or not re.fullmatch(r'[A-Za-z0-9][A-Za-z0-9._-]*', name):
        raise ValueError('logname must contain only ASCII letters, digits, dot, underscore or hyphen')
    return name

class Machine:
    def __init__(self, network=False, logname="smoke", memory="32M", data_disk=None, entropy=True, rtc=None, image="build/notos.img", cpu="max", ready="notos> ", allow_reboot=False):
        self.logname = validate_logname(logname)
        self.tmp = tempfile.TemporaryDirectory(prefix='notos-')
        self.output = bytearray()
        self.sel = selectors.DefaultSelector()
        self.sock = None
        monitor = self.tmp.name + '/monitor'
        args = [os.environ.get('QEMU', 'qemu-system-i386'), '-machine', 'pc', '-cpu', cpu, '-m', memory,
                '-drive', f'format=raw,file={image}', '-display', 'none',
                '-qmp', f'unix:{monitor},server=on,wait=off',
                '-serial', 'stdio']
        if not allow_reboot:
            args += ['-no-reboot']
        if entropy:
            seed = Path(self.tmp.name, 'entropy.bin')
            seed.write_bytes(os.urandom(64)); seed.chmod(0o600)
            args += ['-fw_cfg', f'name=opt/notos/entropy,file={seed}']
        if rtc is not None:
            args += ['-rtc', f'base={rtc},clock=vm']
        if data_disk is not None:
            args += ['-drive', f'if=ide,index=1,format=raw,file={data_disk}']
        if os.environ.get('QEMU_TRACE'):
            args += ['-d', 'int,cpu_reset', '-D', 'build/qemu-trace.log']
        if network:
            args += ['-netdev', 'user,id=net0', '-device', 'rtl8139,netdev=net0']
            if os.environ.get('QEMU_PCAP'):
                args += ['-object', 'filter-dump,id=capture,netdev=net0,file=build/network.pcap']
        else:
            args += ['-nic', 'none']
        self.p = subprocess.Popen(args, stdin=subprocess.PIPE, stdout=subprocess.PIPE, stderr=subprocess.PIPE)
        self.sel.register(self.p.stdout, selectors.EVENT_READ)
        try:
            deadline = time.monotonic()+10
            while not Path(monitor).exists():
                if self.p.poll() is not None:
                    raise AssertionError(self.p.stderr.read().decode())
                if time.monotonic() > deadline:
                    raise AssertionError('monitor did not start')
                time.sleep(.01)
            self.sock = socket.socket(socket.AF_UNIX)
            self.sock.settimeout(10)
            while True:
                try:
                    self.sock.connect(monitor)
                    break
                except (FileNotFoundError, ConnectionRefusedError):
                    if self.p.poll() is not None:
                        raise AssertionError(self.p.stderr.read().decode())
                    if time.monotonic() > deadline:
                        raise AssertionError('monitor did not accept connections')
                    time.sleep(.01)
            self.stream = self.sock.makefile('rwb', buffering=0)
            greeting = json.loads(self.stream.readline())
            assert 'QMP' in greeting
            self.qmp('qmp_capabilities')
            self.expect(ready, 0)
        except BaseException:
            self.close()
            raise
    def qmp(self, command, arguments=None):
        request={'execute': command}
        if arguments is not None: request['arguments']=arguments
        self.stream.write(json.dumps(request).encode()+b'\n')
        while True:
            response=json.loads(self.stream.readline())
            if 'error' in response: raise AssertionError(response)
            if 'return' in response: return response['return']
    def monitor(self, command):
        return self.qmp('human-monitor-command', {'command-line': command}).encode()
    def expect(self, text, start=0, timeout=10):
        deadline=time.monotonic()+timeout
        target=text.encode()
        while target not in self.output[start:]:
            if self.p.poll() is not None:
                raise AssertionError('QEMU exited: '+self.p.stderr.read().decode())
            if time.monotonic()>deadline:
                raise AssertionError(f'Timed out waiting for {text!r}\n'+self.output[start:].decode(errors='replace'))
            for key,_ in self.sel.select(.05):
                block=os.read(key.fileobj.fileno(),65536)
                if block:
                    self.output.extend(block)
        return bytes(self.output[start:])
    def command(self, text, expected, timeout=10):
        start=len(self.output)
        self.p.stdin.write(text.encode()+b'\n')
        self.p.stdin.flush()
        return self.expect(expected,start,timeout)
    def wait_exit(self, timeout=10):
        deadline = time.monotonic() + timeout
        while self.p.poll() is None:
            if time.monotonic() > deadline:
                raise AssertionError('Timed out waiting for QEMU exit\n' + self.output.decode(errors='replace'))
            for key, _ in self.sel.select(.05):
                block = os.read(key.fileobj.fileno(), 65536)
                if block:
                    self.output.extend(block)
        while True:
            block = os.read(self.p.stdout.fileno(), 65536)
            if not block:
                break
            self.output.extend(block)
        return self.p.returncode
    def screenshot(self, name='desktop'):
        path=str(Path('build',name+'.ppm').resolve())
        response=self.monitor('screendump '+path)
        assert not response.strip(), response
        data=Path(path).read_bytes()
        header, width_height, maximum, pixels=data.split(b'\n',3)
        assert header==b'P6' and maximum==b'255', data[:40]
        width,height=map(int,width_height.split())
        assert (width,height) in [(320,200),(640,400)], (width,height)
        assert len(pixels)==width*height*3
        if width==640:  # QEMU displays mode 13h with doubled pixels/scanlines.
            pixels=b''.join(pixels[(y*640+x)*3:(y*640+x+1)*3]
                            for y in range(0,400,2) for x in range(0,640,2))
        return pixels
    def close(self):
        if self.p.poll() is None:
            self.p.terminate()
        self.p.wait(timeout=5)
        Path('build',self.logname+'.log').write_bytes(self.output)
        if hasattr(self, 'stream'): self.stream.close()
        if self.sock: self.sock.close()
        self.sel.close()
        self.tmp.cleanup()
    def __enter__(self): return self
    def __exit__(self,*args): self.close()
