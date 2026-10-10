"""Host fixture only: guest traffic uses RTL8139 -> lwIP -> NotOS syscalls."""
import socket
import socketserver
import struct
import threading
import time
BODY=bytes((i*17+3)&255 for i in range(32768))
class Server(socketserver.ThreadingTCPServer):
    allow_reuse_address=True
    daemon_threads=True
    def __init__(self,mode):
        self.mode=mode;self.errors=[]
        super().__init__(('127.0.0.1',0),Handler)
        self.thread=threading.Thread(target=self.serve_forever,daemon=True);self.thread.start()
    @property
    def port(self):return self.server_address[1]
    def close(self):self.shutdown();self.server_close();self.thread.join()
    def __enter__(self):return self
    def __exit__(self,*args):self.close();assert not self.errors,self.errors
class Handler(socketserver.BaseRequestHandler):
    def handle(self):
        self.request.settimeout(20)
        mode=self.server.mode
        try:
            if mode=='reset':
                assert self.request.recv(1)==b'r'
                time.sleep(.5)
                self.request.setsockopt(socket.SOL_SOCKET,socket.SO_LINGER,struct.pack('ii',1,0))
                # Bypass socketserver's graceful shutdown_request(), which sends FIN first.
                self.request.close();return
            if mode=='pool':
                self.request.recv(1);time.sleep(2);return
            if mode=='hold':
                self.request.recv(1);return
            data=b''
            if mode=='http':
                while b'\r\n\r\n' not in data:data+=self.request.recv(997)
                assert data==b'GET /socket HTTP/1.0\r\nHost: 10.0.2.2\r\n\r\n',data
                response=b'HTTP/1.0 200 OK\r\nContent-Length: 32768\r\n\r\n'+BODY
            else:
                while len(data)<4:data+=self.request.recv(4-len(data))
                target=4 if data==b'ping' else len(BODY)
                while len(data)<target:
                    chunk=self.request.recv(min(997,target-len(data)));assert chunk,'early EOF';data+=chunk
                assert data==(b'ping' if target==4 else BODY)
                response=data
            for i in range(0,len(response),137):self.request.sendall(response[i:i+137])
            self.request.shutdown(socket.SHUT_WR)
            while self.request.recv(1024):pass
        except Exception as error:self.server.errors.append(repr(error))
class DNS:
    def __init__(self):
        self.socket=socket.socket(socket.AF_INET,socket.SOCK_DGRAM)
        self.socket.bind(('127.0.0.1',53530));self.socket.settimeout(.1)
        self.names=[];self.done=False;self.delays=[]
        self.thread=threading.Thread(target=self.run,daemon=True);self.thread.start()
    def run(self):
        while not self.done:
            try:packet,peer=self.socket.recvfrom(1024)
            except socket.timeout:continue
            except OSError:return
            position=12;labels=[]
            while packet[position]:
                n=packet[position];labels.append(packet[position+1:position+1+n].decode());position+=n+1
            position+=5;name='.'.join(labels);self.names.append(name)
            question=packet[12:position];missing=name=='missing.socket.test'
            flags=0x8183 if missing else 0x8180
            answer=packet[:2]+struct.pack('!HHHHH',flags,1,0 if missing else 1,0,0)+question
            if not missing:answer+=b'\xc0\x0c'+struct.pack('!HHIH',1,1,1,4)+bytes([10,0,2,2])
            if name=='slow.socket.test':
                timer=threading.Timer(.6,self.reply,args=(answer,peer));timer.start();self.delays.append(timer)
            else:self.reply(answer,peer)
    def reply(self,answer,peer):
        try:self.socket.sendto(answer,peer)
        except OSError:pass
    def __enter__(self):return self
    def __exit__(self,*args):
        self.done=True;self.thread.join();[t.join() for t in self.delays];self.socket.close()
