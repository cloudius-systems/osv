#!/usr/bin/env python3
# Copyright (C) 2026 Greg Burd.
"""Loopback-only V13 fixture; not a server durability certificate."""
import ctypes
import ctypes.util
import select
import socket
import struct
import subprocess
import sys
import threading
import time

u32 = lambda n: struct.pack('<I', n)
u64 = lambda n: struct.pack('<Q', n)
vec = lambda xs: u64(len(xs)) + b''.join(u64(x) for x in xs)
xx = ctypes.CDLL(ctypes.util.find_library('xxhash'))
xx.XXH64.argtypes = [ctypes.c_void_p, ctypes.c_size_t, ctypes.c_ulonglong]
xx.XXH64.restype = ctypes.c_ulonglong

def exact(s, n):
    out = b''
    while len(out) < n:
        part = s.recv(n-len(out))
        if not part:
            raise EOFError()
        out += part
    return out

class Peer:
    def __init__(self, index):
        self.index = index
        self.listener = socket.socket()
        self.listener.bind(('127.0.0.1', 0))
        self.listener.listen()
        self.listener.settimeout(.1)
        self.port = self.listener.getsockname()[1]
        self.data = b'A'*512
        self.gen = 0
        self.flush = 0
        self.dirty = False
        self.mode = ''
        self.conn = None
        self.sessions = 0
        self.stop = False
        self.errors = []
        self.deferred = None
        self.maxdeps = 0
        self.completed = set()
        self.held_read = None
        self.held = threading.Event()
        self.allow_reconnect = threading.Event(); self.allow_reconnect.set()
        self.t = threading.Thread(target=self.run)
        self.t.start()

    def reply(self, s, msg):
        frame = u32(len(msg)+4)+msg
        if self.mode in ('prefix', 'payload') and msg[:4] == u32(37):
            s.sendall(frame[:2 if self.mode == 'prefix' else 9])
            while not self.stop and self.mode:
                time.sleep(.01)
            return
        if self.mode == 'trickle' and msg[:4] == u32(1):
            for byte in frame:
                s.sendall(bytes([byte])); time.sleep(.4)
            return
        s.sendall(frame)

    def run(self):
        while not self.stop:
            try:
                s, _ = self.listener.accept()
                self.allow_reconnect.wait()
                self.conn = s
                self.sessions += 1
                with s:
                    while not self.stop:
                        size = struct.unpack('<I', exact(s,4))[0]
                        m = exact(s,size-4)
                        kind = struct.unpack_from('<I',m)[0]
                        if kind == 0:
                            self.identity = m[8:56]
                            self.epoch = struct.unpack_from('<Q',m,56)[0]
                            self.reply(s,u32(1)+u32(13)+u32(0)+bytes([127,0,0,1])+struct.pack('<H',1))
                        elif kind == 5:
                            self.reply(s,u32(6)+m[4:60])
                        elif kind == 25:
                            self.reply(s,u32(26)+u64(512)+u64(8)+u32(9)+u32(1)+u64(16)+bytes(16)+bytes([0])+u64(1)+u64(1))
                        elif kind == 27:
                            self.reply(s,u32(28)+vec([self.gen])+vec([self.flush])+u64(1)+bytes([self.dirty])+(b'x' if self.mode == 'admissiontrailing' else b''))
                        elif kind in (30,32,36):
                            job = m[52:60]
                            ndep = struct.unpack_from('<Q',m,60)[0]
                            self.maxdeps = max(self.maxdeps, ndep)
                            pos = 68+8*ndep
                            deps = struct.unpack_from('<'+'Q'*ndep,m,68) if ndep else ()
                            jid = struct.unpack('<Q',job)[0]
                            if self.mode == 'watermark':
                                if self.held_read and self.held_read[0] in deps:
                                    oldid, olddeps, response = self.held_read
                                    assert all(d in self.completed for d in olddeps), 'read dependency forgotten'
                                    self.reply(s,response)
                                    self.completed.add(oldid)
                                    self.held_read = None
                                assert all(d in self.completed for d in deps), 'job references forgotten/incomplete dependency'
                                if kind == 36:
                                    h = xx.XXH64(self.data,len(self.data),0)
                                    response = u32(37)+self.identity+job+u32(0)+u64(1)+u32(2)+u64(h)+u64(512)+self.data
                                    self.held_read = (jid,deps,response)
                                    self.held.set()
                                    continue
                                if kind == 32 and self.held_read:
                                    raise AssertionError('flush reset overtook outstanding read')
                            if kind == 32: self.completed.clear()
                            self.completed.add(jid)
                            if self.deferred and self.deferred[0] in deps:
                                self.data = self.deferred[1]
                                self.reply(s,u32(31)+self.identity+u64(self.deferred[0])+u32(0))
                                self.deferred = None
                            if kind == 30 and self.mode == 'lagwrite':
                                self.deferred = (struct.unpack('<Q',job)[0],m[-512:])
                                self.held.set()
                                continue
                            if kind == 30:
                                self.data = m[-512:]
                                self.dirty = True
                                self.reply(s,u32(31)+self.identity+job+u32(0))
                            elif kind == 32:
                                if self.mode == 'snapshotloss':
                                    s.shutdown(socket.SHUT_RDWR); break
                                self.flush,self.gen = struct.unpack_from('<QQ',m,pos)
                                self.dirty = False
                                self.reply(s,u32(33)+self.identity+job+u32(0))
                            else:
                                if self.mode == 'slow': time.sleep(.2)
                                h = xx.XXH64(self.data,len(self.data),0)
                                context = u32(99) if self.mode == 'badtype' else u32(2)+u64(h)
                                identity = (u64(16)+bytes(16))*2 if self.mode == 'identity' else self.identity
                                data = self.data
                                if self.mode == 'badhash': context = u32(2)+u64(h ^ 1)
                                if self.mode == 'empty': context = u32(0)
                                if self.mode == 'emptyzero': context = u32(0); data = bytes(512)
                                if self.mode == 'encrypted': context = u32(1)+bytes(28)
                                if self.mode == 'short': data = data[:-1]
                                if self.mode == 'contexts': context += context
                                count = 2 if self.mode == 'contexts' else 1
                                response = u32(37)+identity+job+u32(0)+u64(count)+context+u64(len(data))+data
                                if self.mode == 'wrongkind': response = u32(31)+identity+job+u32(0)
                                if self.mode == 'hugecount': response = u32(37)+identity+job+u32(0)+u64(2**64-1)
                                if self.mode == 'hugelen': response = u32(37)+identity+job+u32(0)+u64(1)+context+u64(2**64-1)
                                if self.mode == 'fatal': response = u32(8)
                                if self.mode == 'trailing': response += b'x'
                                self.reply(s,response)
                        elif kind == 9:
                            self.reply(s,u32(10))
                        else:
                            raise AssertionError(('unexpected message',kind))
            except (OSError, EOFError):
                pass
            except Exception as e:
                self.errors.append(repr(e))
                return

    def drop(self):
        if self.conn:
            try: self.conn.shutdown(socket.SHUT_RDWR)
            except OSError: pass

    def close(self):
        self.stop = True
        self.allow_reconnect.set()
        self.mode = ''
        self.drop()
        self.listener.close()
        self.t.join(timeout=3)
        assert not self.t.is_alive(), "fixture peer failed to stop"

class Client:
    def __init__(self, binary, peers, targets=None):
        self.p = subprocess.Popen([binary]+(targets or ['127.0.0.1:'+str(p.port) for p in peers]),stdin=subprocess.PIPE,stdout=subprocess.PIPE,stderr=sys.stderr)
    def send(self, cmd):
        self.p.stdin.write(cmd.encode()+b'\n'); self.p.stdin.flush()
    def result(self, deadline=2):
        assert select.select([self.p.stdout],[],[],deadline)[0], 'client response deadline expired'
        return self.p.stdout.readline().decode().strip()
    def cmd(self, cmd):
        self.send(cmd); return self.result()
    def close(self):
        self.p.kill(); self.p.wait()

def run(binary, scenario):
    peers = [Peer(i) for i in range(3)]
    targets = ['127.0.0.1:'+str(p.port) for p in peers]
    invalid = {
        'hostname': 'localhost:1234', 'ipv6': '[::1]:1234',
        'portoverflow': '127.0.0.1:'+str(peers[2].port+65536),
        'portjunk': targets[2]+'x', 'portsign': '127.0.0.1:+'+str(peers[2].port),
        'portzero': '127.0.0.1:0', 'portempty': '127.0.0.1:',
    }
    if scenario in invalid: targets[2] = invalid[scenario]
    c = Client(binary,peers,targets)
    try:
        if scenario in invalid:
            assert c.cmd('c').startswith('error'), 'invalid target admitted'
            assert not any(p.sessions for p in peers), 'invalid configuration promoted earlier peers'
            return
        if scenario == 'admissiontrailing':
            for p in peers: p.mode = scenario
        if scenario == 'divergent': peers[0].flush = 8
        if scenario == 'overflow':
            for p in peers: p.flush = 2**64-2
        if scenario == 'dirty':
            for p in peers: p.dirty = True
        if scenario == 'admission':
            assert c.cmd('a') == 'admission-stopped 1', 'connect started workers after disconnect'
            return
        if scenario == 'trickle':
            peers[0].mode = 'trickle'
            c.send('c')
            assert c.result(6).startswith('error'), 'admission lacks total deadline'
            return
        answer = c.cmd('c')
        if scenario in ('divergent','dirty','admissiontrailing'):
            assert answer.startswith('error'), ('unsafe initial admission',answer)
            return
        assert answer == 'connected', answer
        assert c.cmd('r') == 'read 0 65'
        if scenario == 'disconnects':
            assert c.cmd('x') == 'both-stopped'
        elif scenario == 'control':
            assert c.cmd('w') == 'write 0'
            assert c.cmd('f') == 'flush 0'
            assert c.cmd('r') == 'read 0 66'
        elif scenario == 'generation':
            assert c.cmd('w') == 'write 0'
            assert c.cmd('f') == 'flush 0'
            time.sleep(.1)
            assert c.cmd('g 1').startswith('error'), 'reused generation admitted'
            assert c.cmd('g 2') == 'reopened', 'explicit higher generation refused'
        elif scenario == 'overlap':
            assert c.cmd('o') == 'overlap 0 0 0 0'
        elif scenario == 'snapshotloss':
            peers[0].mode = 'snapshotloss'
            assert c.cmd('s') == 'snapshot 5', '3/3 snapshot survived first loss'
            assert c.cmd('i') == 'online 0'
        elif scenario == 'watermark':
            peers[0].mode = 'watermark'
            assert c.cmd('r') == 'read 0 65'
            assert peers[0].held.wait(2)
            assert c.cmd('f') == 'flush 0'
            assert c.cmd('w') == 'write 0'
            assert c.cmd('r') == 'read 0 66'
            assert c.cmd('f') == 'flush 0'
            time.sleep(.1)
        elif scenario == 'frontier':
            for _ in range(128): assert c.cmd('r') == 'read 0 65'
            assert c.cmd('f') == 'flush 0'
            assert max(p.maxdeps for p in peers) <= 1, 'serialized dependency frontier grows with history'
        elif scenario == 'pressure':
            assert c.cmd('b') == 'backpressure'
            for _ in range(128): assert c.cmd('w') == 'write 0'
            assert c.cmd('v') == 'backlog-quarantined 1', 'sender queue accepted unbounded backlog'
            # Quarantined backlog peer cannot support a later quorum.
            for p in peers: p.drop()
            assert c.cmd('w') == 'write 5'
            assert c.cmd('i') == 'online 0'
        elif scenario == 'allocation':
            assert c.cmd('n') == 'allocation-fenced 1 sent 1', 'partial enqueue exception did not fence session'
        elif scenario == 'overflow':
            assert c.cmd('f') == 'flush 0'
            assert c.cmd('f') == 'flush 5'
            assert c.cmd('i') == 'online 0', 'flush exhaustion left admitted unsent frontier'
        elif scenario == 'stale':
            peers[0].drop()
            # Reconnect backoff gives surviving peers time to acknowledge.
            assert c.cmd('w') == 'write 0'
            assert c.cmd('f') == 'flush 0'
            peers[0].allow_reconnect.set()
            time.sleep(.5)
            peers[2].mode = 'slow'
            assert c.cmd('r') == 'read 0 66', 'stale self-consistent hash accepted'
        elif scenario == 'lagwrite':
            peers[0].mode = 'lagwrite'
            assert c.cmd('w') == 'write 0'
            assert peers[0].held.wait(2), 'lag schedule not reached'
            peers[2].mode = 'slow'
            assert c.cmd('r') == 'read 0 66', 'read overtook prior acknowledged write'
        elif scenario in ('prefix','payload'):
            peers[0].mode = scenario
            assert c.cmd('r') == 'read 0 65', 'one partial frame blocked healthy quorum'
        elif scenario == 'shutdown':
            for p in peers: p.mode = 'payload'
            assert c.cmd('h') == 'stopped', 'shutdown failed to wake pending read and join'
        elif scenario.startswith('quorum-'):
            # Exactly one valid response: peer 1 sends malformed data, peer 2
            # disappears. Crediting peer 1 is the only possible false quorum.
            peers[1].mode = scenario[7:]
            peers[2].drop()
            assert c.cmd('r') == 'read 5 0', 'malformed second response credited as quorum'
        elif scenario == 'emptyzero':
            for p in peers: p.mode = 'emptyzero'
            assert c.cmd('r') == 'read 0 0'
        elif scenario in ('badtype', 'identity'):
            for p in peers: p.mode = scenario
            assert c.cmd('r') == 'read 5 0', 'invalid protocol response became authoritative'
        elif scenario == 'sendfail':
            assert c.cmd('e') == 'armed'
            c.send('w')
            assert c.result() == 'write 5', 'sender failure did not fail pending request'
            assert c.cmd('i') == 'online 0', 'sender failure left connected count inflated'
        assert c.cmd('d') == 'disconnected'
        c.send('q'); assert c.p.wait(timeout=2) == 0
        assert not any(p.errors for p in peers), [p.errors for p in peers]
    finally:
        c.close()
        for p in peers: p.close()

if __name__ == '__main__':
    run(sys.argv[1],sys.argv[2])
    print('PASS',sys.argv[2])
