#!/usr/bin/env python3
"""Exercise process replacement, dynamic directories and real serial/PS2 editing."""
import re
import time
from harness import Machine

def snapshot(machine):
    frames=re.search(rb'free_frames=(\d+)',machine.command('mem','\nnotos> '))[1]
    heap=re.search(rb'HEAP: [^\n]+\n',machine.command('heap','\nnotos> '))[0]
    return frames,heap
for memory in ('16M','64M'):
    with Machine(memory=memory,logname='continuation-'+memory) as machine:
        baseline=snapshot(machine)
        start=len(machine.output)
        reply=machine.command('spawn-elf /system/libc.elf exec-source','notos> ')
        pid=int(re.search(rb'ELF: started pid=(\d+)',reply)[1])
        machine.expect(f'PROCESS: pid={pid} exit=0',start)
        assert b'EXEC: same PID, inherited offset/cwd, CLOEXEC and replacement PASS' in machine.output[start:]
        assert snapshot(machine)==baseline
        machine.command('mkdir /tmp/tree','FS: directory updated\nnotos> ')
        machine.command('cd /tmp/tree','\nnotos> ')
        machine.command('write file data','FS: file updated\nnotos> ')
        assert b'not empty' in machine.command('rmdir /tmp/tree','\nnotos> ')
        machine.command('rm file','FS: file removed\nnotos> ')
        machine.command('cd /tmp','\nnotos> ')
        machine.command('rmdir tree','FS: directory updated\nnotos> ')
        machine.command('echo serial-ac\x1b[Db','\nserial-abc\nnotos> ')
        machine.command('\x1b[A','\nserial-abc\nnotos> ')
        machine.command('echo endX\x1b[D\x1b[3~\x1b[H\x1b[F','\nend\nnotos> ')
        machine.command('pw\t','\n/tmp\nnotos> ')
        machine.command('write /tmp/completion data','FS: file updated\nnotos> ')
        machine.command('cat /tmp/compl\t','\ndata\nnotos> ')
        machine.command('rm /tmp/completion','FS: file removed\nnotos> ')
        start=len(machine.output)
        for key in ('e','c','h','o','spc','a','c','left','b','ret'):
            machine.monitor('sendkey '+key)
            time.sleep(.03)
        machine.expect('\nabc\nnotos> ',start)
        assert snapshot(machine)==baseline
    print('PASS: exec/CLOEXEC/cwd, directories, serial history/editing/completion and PS2 arrows on '+memory)

# Download the actual guest ELF through curl, persist it and execute after reboot.
import socketserver
import tempfile
import threading
from pathlib import Path
class ELFHandler(socketserver.BaseRequestHandler):
    def handle(self):
        self.request.recv(4096)
        body=Path('build/libc-app.elf').read_bytes()
        self.request.sendall(b'HTTP/1.0 200 OK\r\nContent-Length: '+str(len(body)).encode()+b'\r\n\r\n'+body)
with tempfile.TemporaryDirectory() as directory:
    disk=Path(directory,'data.img');disk.write_bytes(bytes(128*1024))
    boot=Path('build/notos.img').read_bytes()
    with socketserver.ThreadingTCPServer(('127.0.0.1',0),ELFHandler) as peer:
        thread=threading.Thread(target=peer.serve_forever,daemon=True);thread.start()
        try:
            with Machine(network=True,data_disk=disk,logname='exec-disk-create') as machine:
                machine.command('mkfs','FS: formatted dedicated data disk\nnotos> ')
                machine.command('process-trace on','PROCESS: trace on\nnotos> ')
                from curl import run
                run(machine,f'-sS --create-dirs -o /disk/apps/libc.elf http://10.0.2.2:{peer.server_address[1]}/app')
                machine.command('mkdir /tmp/apps','FS: directory updated\nnotos> ')
                run(machine,f'-sS -o /tmp/apps/libc.elf http://10.0.2.2:{peer.server_address[1]}/app')
                start=len(machine.output)
                reply=machine.command('spawn-elf /tmp/apps/libc.elf exec-source','notos> ')
                pid=int(re.search(rb'ELF: started pid=(\d+)',reply)[1])
                machine.expect(f'PROCESS: pid={pid} exit=0',start)
                assert b'EXEC: same PID' in machine.output[start:]
        finally:peer.shutdown();thread.join()
    with Machine(data_disk=disk,logname='exec-disk-reboot') as machine:
        baseline=snapshot(machine);start=len(machine.output)
        reply=machine.command('spawn-elf /disk/apps/libc.elf exec-source','notos> ')
        pid=int(re.search(rb'ELF: started pid=(\d+)',reply)[1])
        machine.expect(f'PROCESS: pid={pid} exit=0',start)
        assert b'EXEC: same PID' in machine.output[start:] and snapshot(machine)==baseline
    assert Path('build/notos.img').read_bytes()==boot
print('PASS: curl create-dirs/persistent binary writes, VFS ELF from tmpfs/disk, reboot and boot image intact')
