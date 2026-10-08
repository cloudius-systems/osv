#!/usr/bin/env python3
#
# Copyright (C) 2026 Greg Burd
#
# This work is open source software, licensed under the terms of the
# BSD license as described in the LICENSE file in the top-level directory.
#
# 9P over TCP interoperability for tests/tst-9p-tcp.so and tests/tst-9p-fs.so,
# run from the top of an OSv tree whose image already holds both. They are
# built only with conf_fs_9p=1 conf_fs_9p_tcp=1 (conf/kconfig/fs), so pass
# both to the kernel and tests-module builds that make the image:
#
#   scripts/tests/test_9p.py --arch x86_64 --evidence-dir DIR
#
# 1. diod: an existing diod binary (never installed here) serves a private
#    export created under DIR: no authentication, no user database, one
#    worker per export, listening on 127.0.0.1 only, which a QEMU
#    user-network guest reaches as 192.168.122.1. Run as the invoking
#    (nonprivileged) user, diod lets only that uid attach.
#    tst-9p-fs then mounts the same export through mount(2).
# 2. peer: a fault-only peer (fragmentation and coalescing, a partial frame
#    then EOF, a stall, a reset); it supplements diod, never replaces it.
# 3. fs peer: mount failures (Rlerror to Tattach, a 9P2000.u Rversion, an
#    Rlerror to the root Tgetattr); it reports any fid left unclunked.
#
# Both runs are captured with tcpdump on lo (the host side of QEMU's user
# network) and with a QEMU filter-dump on the guest NIC. The 9P messages are
# decoded from the lo capture by the small parser below, independent of
# the OSv codec. Exit status is 0 only if every expected PASS line, a zero
# failure SUMMARY, the decoded wire messages (whole frames only, except the
# peer's two scripted cuts) and the host-side file check are all present.
# run.py's own exit status is not evidence.

import argparse, json, os, signal, socket, struct, subprocess, sys
import tempfile, threading, time

GUEST_HOST = '192.168.122.1'
NAMES = {7: 'Rlerror', 12: 'Tlopen', 13: 'Rlopen', 100: 'Tversion',
         101: 'Rversion', 104: 'Tattach', 105: 'Rattach', 108: 'Tflush',
         109: 'Rflush', 110: 'Twalk', 111: 'Rwalk', 116: 'Tread',
         24: 'Tgetattr', 25: 'Rgetattr',
         117: 'Rread', 118: 'Twrite', 119: 'Rwrite', 120: 'Tclunk',
         121: 'Rclunk'}
HELLO = b'hello from the 9p export\n'
WRITTEN = b'written by OSv over 9P/TCP\n'
DIOD_PASS = ['diod negotiate', 'diod attach', 'diod ENOENT', 'diod read',
             'diod write', 'diod flush', 'diod clunk and shutdown']
PEER_PASS = ['peer fragmentation and coalescing',
             'peer partial frame then EOF', 'peer stall mid-frame',
             'peer reset']
FS_DIOD_PASS = ['diod mount and unmount', 'diod mounts are distinct',
                'diod refusals']
FS_PEER_PASS = ['peer bad aname', 'peer wrong version',
                'peer root getattr error']
DIOD_WIRE = ['Tversion', 'Rversion', 'Tattach', 'Rattach', 'Twalk', 'Rwalk',
             'Tlopen', 'Rlopen', 'Tread', 'Rread', 'Twrite', 'Rwrite',
             'Tclunk', 'Rclunk', 'Tflush', 'Rflush', 'Rlerror', 'Tgetattr',
             'Rgetattr']


def verdict(console, wanted):
    """Problems with a guest console; empty means it passed."""
    bad = [l for l in console.splitlines()
           if l.startswith('FAIL') or 'Aborted' in l or 'panic' in l]
    bad += ['missing PASS: ' + w for w in wanted
            if 'PASS: ' + w not in console.splitlines()]
    if not any(l.startswith('SUMMARY: ') and l.endswith(' 0 failures')
               for l in console.splitlines()):
        bad.append('no zero-failure SUMMARY')
    return bad


# ---- pcap -> TCP byte streams -> 9P messages ----

def flows(path):
    data = open(path, 'rb').read()
    if len(data) < 24 or struct.unpack('<I', data[:4])[0] != 0xa1b2c3d4:
        raise ValueError(path + ': not a little-endian pcap')
    link = struct.unpack('<I', data[20:24])[0]
    off, out = 24, {}
    while off + 16 <= len(data):
        incl = struct.unpack('<I', data[off + 8:off + 12])[0]
        pkt = data[off + 16:off + 16 + incl]
        off += 16 + incl
        if link == 1 and pkt[12:14] == b'\x08\x00':
            ip = pkt[14:]
        elif link == 113 and pkt[14:16] == b'\x08\x00':
            ip = pkt[16:]
        else:
            continue
        if len(ip) < 20 or ip[0] >> 4 != 4 or ip[9] != 6:
            continue
        ihl = (ip[0] & 15) * 4
        tcp = ip[ihl:struct.unpack('>H', ip[2:4])[0]]
        sp, dp, seq = struct.unpack('>HHI', tcp[:8])
        doff, fl = (tcp[12] >> 4) * 4, tcp[13]
        key = '%s:%d>%s:%d' % (socket.inet_ntoa(ip[12:16]), sp,
                               socket.inet_ntoa(ip[16:20]), dp)
        f = out.setdefault(key, {'segs': [], 'syn': None, 'fin': 0,
                                 'rst': 0, 'data_segments': 0})
        if fl & 2:
            f['syn'] = (seq + 1) & 0xffffffff
        f['fin'] += bool(fl & 1)
        f['rst'] += bool(fl & 4)
        if tcp[doff:]:
            f['segs'].append((seq, tcp[doff:]))
            f['data_segments'] += 1
    for f in out.values():
        base = f['syn'] if f['syn'] is not None else (
            min(s for s, _ in f['segs']) if f['segs'] else 0)
        buf = bytearray()
        for seq, p in sorted(f['segs'], key=lambda s: (s[0] - base) & 0xffffffff):
            at = (seq - base) & 0xffffffff
            if at + len(p) > len(buf) and at <= len(buf):
                buf += p[len(buf) - at:]
        f['bytes'] = bytes(buf)
        del f['segs']
    return out


def messages(b):
    msgs, off = [], 0
    while off + 7 <= len(b):
        size, typ, tag = struct.unpack('<IBH', b[off:off + 7])
        if size < 7 or off + size > len(b):
            break
        body = b[off + 7:off + size]
        m = {'type': NAMES.get(typ, str(typ)), 'tag': tag, 'size': size}
        if typ in (100, 101):
            n = struct.unpack('<H', body[4:6])[0]
            m['msize'] = struct.unpack('<I', body[:4])[0]
            m['version'] = body[6:6 + n].decode('ascii', 'replace')
        elif typ == 7:
            m['ecode'] = struct.unpack('<I', body[:4])[0]
        elif typ == 108:
            m['oldtag'] = struct.unpack('<H', body[:2])[0]
        elif typ == 117:   # Rread count[4] data
            m['count'] = struct.unpack('<I', body[:4])[0]
        elif typ == 118:   # Twrite fid[4] offset[8] count[4] data
            m['count'] = struct.unpack('<I', body[12:16])[0]
        msgs.append(m)
        off += size
    return msgs, len(b) - off


def decode(path, port):
    """Per-connection 9P messages of a capture, for connections to port."""
    out = []
    for key, f in sorted(flows(path).items()):
        if not (key.endswith(':%d' % port) or (':%d>' % port) in key):
            continue
        msgs, rest = messages(f['bytes'])
        out.append({'flow': key, 'bytes': len(f['bytes']),
                    'data_segments': f['data_segments'], 'fin': f['fin'],
                    'rst': f['rst'], 'trailing_bytes': rest,
                    'messages': msgs})
    return out


# ---- the fault-only peer ----

def recv_frame(c):
    hdr = b''
    while len(hdr) < 7:
        k = c.recv(7 - len(hdr))
        if not k:
            return None
        hdr += k
    size, typ, tag = struct.unpack('<IBH', hdr)
    body = b''
    while len(body) < size - 7:
        k = c.recv(size - 7 - len(body))
        if not k:
            return None
        body += k
    return typ, tag, body


def reply(typ, tag, body):
    return struct.pack('<IBH', 7 + len(body), typ, tag) + body


def answer(typ, tag, body):
    if typ == 100:
        v = b'9P2000.L'
        return reply(101, tag, struct.pack('<IH', min(8192, struct.unpack(
            '<I', body[:4])[0]), len(v)) + v)
    if typ == 104:
        return reply(105, tag, b'\x80' + bytes(12))
    if typ == 116:
        return reply(117, tag, struct.pack('<I', 4) + b'frag')
    return None


def dribble(c, b):
    for i in range(len(b)):
        c.sendall(b[i:i + 1])
        time.sleep(0.002)


def peer_conn(c, case, log):
    c.setsockopt(socket.IPPROTO_TCP, socket.TCP_NODELAY, 1)
    reads = 0
    pending = []
    while True:
        f = recv_frame(c)
        if f is None:
            log.append('%s: client closed' % case)
            return
        typ, tag, body = f
        log.append('%s: got %s tag %d' % (case, NAMES.get(typ, typ), tag))
        r = answer(typ, tag, body)
        if typ != 116:
            if r and case == 'frag':
                dribble(c, r)
            elif r:
                c.sendall(r)
            continue
        if case == 'frag':
            reads += 1
            if reads <= 4:
                dribble(c, r)   # one byte per segment
                continue
            pending.append(r)
            if len(pending) == 6:
                c.sendall(b''.join(pending))   # six replies, one send
                pending = []
        elif case == 'eof':
            c.sendall(r[:9])
            c.shutdown(socket.SHUT_WR)
        elif case == 'stall':
            c.sendall(r[:5])   # then nothing; the connection stays open
        elif case == 'reset':
            c.setsockopt(socket.SOL_SOCKET, socket.SO_LINGER,
                         struct.pack('ii', 1, 0))
            c.close()
            log.append('reset: sent RST')
            return


def peer(port, log, stop):
    ls = socket.socket()
    ls.setsockopt(socket.SOL_SOCKET, socket.SO_REUSEADDR, 1)
    ls.bind(('127.0.0.1', port))
    ls.listen(1)
    ls.settimeout(1)
    for case in ['frag', 'eof', 'stall', 'reset']:
        while not stop.is_set():
            try:
                c, _ = ls.accept()
                break
            except socket.timeout:
                pass
        else:
            break
        try:
            peer_conn(c, case, log)
        except OSError as e:
            log.append('%s: %s' % (case, e))
        try:
            c.close()
        except OSError:
            pass
    ls.close()


# The mount-failure peer for tst-9p-fs, one connection per case. Fids
# attached and never clunked are reported as 'unclunked' lines.
FS_CASES = ['aname', 'version', 'getattr']


def fs_peer_conn(c, case, log):
    fids = set()
    while True:
        f = recv_frame(c)
        if f is None:
            break
        typ, tag, body = f
        log.append('fs-%s: got %s tag %d' % (case, NAMES.get(typ, typ), tag))
        if typ == 100:
            v = b'9P2000.u' if case == 'version' else b'9P2000.L'
            c.sendall(reply(101, tag, struct.pack('<IH', min(
                8192, struct.unpack('<I', body[:4])[0]), len(v)) + v))
        elif typ == 104 and case == 'aname':
            c.sendall(reply(7, tag, struct.pack('<I', 2)))     # ENOENT
        elif typ == 104:
            fids.add(struct.unpack('<I', body[:4])[0])
            c.sendall(reply(105, tag, b'\x80' + bytes(12)))
        elif typ == 24:
            c.sendall(reply(7, tag, struct.pack('<I', 13)))    # EACCES
        elif typ == 120:
            fids.discard(struct.unpack('<I', body[:4])[0])
            c.sendall(reply(121, tag, b''))
        else:
            c.sendall(reply(7, tag, struct.pack('<I', 95)))    # EOPNOTSUPP
    log.append('fs-%s: client closed' % case)
    if fids:
        log.append('fs-%s: %d unclunked fids' % (case, len(fids)))


def fs_peer(port, log, stop):
    ls = socket.socket()
    ls.setsockopt(socket.SOL_SOCKET, socket.SO_REUSEADDR, 1)
    ls.bind(('127.0.0.1', port))
    ls.listen(1)
    ls.settimeout(1)
    for case in FS_CASES:
        while not stop.is_set():
            try:
                c, _ = ls.accept()
                break
            except socket.timeout:
                pass
        else:
            break
        try:
            fs_peer_conn(c, case, log)
        except OSError as e:
            log.append('fs-%s: %s' % (case, e))
        c.close()
    ls.close()


# ---- orchestration ----

def capture(ev, name, ports):
    pidfile = os.path.join(ev, name + '.pid')
    cmd = ('echo $$ > %s; exec tcpdump -i lo -U -s 0 -Z %s -w %s %s'
           % (pidfile, os.environ.get('USER', 'admin'),
              os.path.join(ev, name + '.pcap'),
              ' or '.join('tcp port %d' % p for p in ports)))
    p = subprocess.Popen(['sudo', 'sh', '-c', cmd],
                         stderr=open(os.path.join(ev, name + '.log'), 'w'))
    time.sleep(2)
    return p, pidfile


def stop_capture(p, pidfile):
    time.sleep(1)
    subprocess.run(['sudo', 'kill', '-INT', open(pidfile).read().strip()])
    p.wait(timeout=30)


def guest(args, ev, name, cmd, test='tst-9p-tcp'):
    out = os.path.join(ev, name + '.console')
    run = ['timeout', '300', sys.executable, 'scripts/run.py', '-m', '2G',
           '-c', '2', '--arch', args.arch, '--nogdb', '--pass-args',
           '-object filter-dump,id=fd0,netdev=un0,file=%s'
           % os.path.join(ev, name + '-guest.pcap'),
           '-e', '--rootfs=rofs /tests/%s.so %s' % (test, cmd)]
    with open(os.path.join(ev, name + '.command'), 'w') as f:
        f.write(' '.join(run) + '\n')
    rc = subprocess.run(run, stdout=open(out, 'w'),
                        stderr=subprocess.STDOUT).returncode
    return open(out, errors='replace').read(), rc


def run_diod(args, ev, problems):
    root = tempfile.mkdtemp(prefix='osv9p.', dir=ev)
    os.chmod(root, 0o700)
    a, b = os.path.join(root, 'a'), os.path.join(root, 'b')
    os.mkdir(a, 0o700)
    os.mkdir(b, 0o700)
    open(os.path.join(a, 'hello'), 'wb').write(HELLO)
    open(os.path.join(a, 'big'), 'wb').write(
        bytes(i % 251 for i in range(1000000)))
    open(os.path.join(a, 'out'), 'wb').close()
    os.mkfifo(os.path.join(a, 'fifo'), 0o600)
    os.link(os.path.join(a, 'fifo'), os.path.join(b, 'fifo'))
    cmd = [args.diod, '-f', '-n', '-N', '-t', '1', '-c', '/dev/null',
           '-d', '1', '-L', 'stderr', '-l', '127.0.0.1:%d' % args.port,
           '-e', a, '-e', b]
    with open(os.path.join(ev, 'diod.command'), 'w') as f:
        f.write(' '.join(cmd) + '\n')
    d = subprocess.Popen(cmd, stderr=open(os.path.join(ev, 'diod.log'), 'w'))
    try:
        time.sleep(1)
        subprocess.run('ss -ltnp > %s' % os.path.join(ev, 'diod-listen.txt'),
                       shell=True)
        console, _ = guest(args, ev, 'diod', 'diod %s %d %d %s %s' % (
            GUEST_HOST, args.port, os.getuid(), a, b))
        fs_console, _ = guest(args, ev, 'fs-diod', 'diod %s %d %d %s' % (
            GUEST_HOST, args.port, os.getuid(), a), 'tst-9p-fs')
    finally:
        # Reaped even if the guest run raises.
        d.send_signal(signal.SIGTERM)
        try:
            d.wait(timeout=30)
        except subprocess.TimeoutExpired:
            d.kill()
            d.wait()
    problems += ['diod: ' + p for p in verdict(console, DIOD_PASS)]
    problems += ['fs-diod: ' + p for p in verdict(fs_console, FS_DIOD_PASS)]
    if open(os.path.join(a, 'out'), 'rb').read() != WRITTEN:
        problems.append('diod: host file a/out does not hold the Twrite bytes')
    # The guest clunks every fid it creates; diod lists any it still holds.
    leaks = [l for l in open(os.path.join(ev, 'diod.log'))
             if 'unclunked' in l]
    problems += ['diod: server reports ' + l.strip() for l in leaks]


def run_peer(args, ev, problems):
    log, stop = [], threading.Event()
    t = threading.Thread(target=peer, args=(args.peer_port, log, stop))
    t.start()
    console, _ = guest(args, ev, 'peer', 'peer %s %d' % (GUEST_HOST,
                                                         args.peer_port))
    stop.set()
    t.join(timeout=30)
    open(os.path.join(ev, 'peer.log'), 'w').write('\n'.join(log) + '\n')
    problems += ['peer: ' + p for p in verdict(console, PEER_PASS)]


def run_fs_peer(args, ev, problems):
    log, stop = [], threading.Event()
    t = threading.Thread(target=fs_peer, args=(args.fs_peer_port, log, stop))
    t.start()
    console, _ = guest(args, ev, 'fs-peer', 'peer %s %d' % (
        GUEST_HOST, args.fs_peer_port), 'tst-9p-fs')
    stop.set()
    t.join(timeout=30)
    open(os.path.join(ev, 'fs-peer.log'), 'w').write('\n'.join(log) + '\n')
    problems += ['fs-peer: ' + p for p in verdict(console, FS_PEER_PASS)]
    problems += fs_peer_log(log)


# Every case connected, was closed by the client and left no fid.
def fs_peer_log(log):
    bad = ['fs-peer: ' + l for l in log if 'unclunked' in l]
    bad += ['fs-peer: case %s never closed' % c for c in FS_CASES
            if 'fs-%s: client closed' % c not in log]
    return bad


def check_wire(ev, problems):
    d = decode(os.path.join(ev, 'host-lo.pcap'), ARGS.port)
    p = decode(os.path.join(ev, 'host-lo.pcap'), ARGS.peer_port)
    g = {'diod': decode(os.path.join(ev, 'diod-guest.pcap'), ARGS.port),
         'peer': decode(os.path.join(ev, 'peer-guest.pcap'), ARGS.peer_port)}
    json.dump({'diod_lo': d, 'peer_lo': p, 'guest': g},
              open(os.path.join(ev, 'wire.json'), 'w'), indent=1)
    seen = {m['type'] for f in d for m in f['messages']}
    problems += ['wire: no %s to or from diod' % t for t in DIOD_WIRE
                 if t not in seen]
    if not any(m['type'] == 'Rversion' and m['version'] == '9P2000.L'
               for f in d for m in f['messages']):
        problems.append('wire: no Rversion 9P2000.L')
    if not any(m.get('ecode') == 2 for f in d for m in f['messages']):
        problems.append('wire: no Rlerror ENOENT')
    if len(p) < 8 or not any(f['rst'] for f in p):
        problems.append('wire: peer connections or RST missing')
    problems += trailing(d, p)


# Bytes after the last whole message of a flow. Only the peer's server side
# may end inside a frame, and only where the fault script cuts one: 9 bytes
# before EOF (eof) and 5 before the stall (stall). Anything else means the
# decoder lost frame sync or a sender wrote a partial frame.
PEER_CUTS = [5, 9]


def trailing(d, p):
    bad = ['wire: %d trailing bytes on diod flow %s' % (f['trailing_bytes'],
                                                        f['flow'])
           for f in d if f['trailing_bytes']]
    bad += ['wire: %d trailing bytes on client flow %s'
            % (f['trailing_bytes'], f['flow'])
            for f in p if f['trailing_bytes'] and not is_server(f)]
    cuts = sorted(f['trailing_bytes'] for f in p
                  if f['trailing_bytes'] and is_server(f))
    if cuts != PEER_CUTS:
        bad.append('wire: peer partial frames %s, want %s' % (cuts,
                                                              PEER_CUTS))
    return bad


def is_server(f):
    return f['flow'].startswith('127.0.0.1:%d>' % ARGS.peer_port)


def selftest():
    good = 'PASS: x\nSUMMARY: 3 tcp checks, 0 failures\n'
    assert verdict(good, ['x']) == []
    assert verdict(good, ['x', 'y'])
    assert verdict('PASS: x\nFAIL: y (check 2)\n', ['x'])
    assert verdict('PASS: x\n', ['x'])
    assert verdict(good.replace('PASS: x', 'PASS: xx'), ['x'])
    m, rest = messages(reply(101, 0xffff, struct.pack('<IH', 8192, 8)
                             + b'9P2000.L') + reply(7, 1, struct.pack('<I', 2))
                       + b'\x30\x00')
    assert [x['type'] for x in m] == ['Rversion', 'Rlerror'] and rest == 2
    assert m[0]['msize'] == 8192 and m[1]['ecode'] == 2
    sv = '127.0.0.1:%d>127.0.0.1:1' % ARGS.peer_port
    cl = '127.0.0.1:1>127.0.0.1:%d' % ARGS.peer_port
    ok = [{'flow': sv, 'trailing_bytes': 9}, {'flow': sv, 'trailing_bytes': 5},
          {'flow': cl, 'trailing_bytes': 0}]
    assert trailing([{'flow': 'x', 'trailing_bytes': 0}], ok) == []
    assert trailing([{'flow': 'x', 'trailing_bytes': 3}], ok)
    assert trailing([], ok[:1] + ok[2:])
    assert trailing([], ok + [{'flow': cl, 'trailing_bytes': 2}])
    closed = ['fs-%s: client closed' % c for c in FS_CASES]
    assert fs_peer_log(closed) == []
    assert fs_peer_log(closed[1:])
    assert fs_peer_log(closed + ['fs-getattr: 1 unclunked fids'])
    print('selftest ok')


def main():
    global ARGS
    ap = argparse.ArgumentParser()
    ap.add_argument('--arch', default=os.uname().machine)
    ap.add_argument('--evidence-dir')
    ap.add_argument('--diod', default='/usr/sbin/diod')
    ap.add_argument('--port', type=int, default=5640)
    ap.add_argument('--peer-port', type=int, default=5641)
    ap.add_argument('--fs-peer-port', type=int, default=5642)
    ap.add_argument('--selftest', action='store_true')
    ARGS = args = ap.parse_args()
    selftest()
    if args.selftest:
        return 0
    ev = os.path.abspath(args.evidence_dir)
    os.makedirs(ev)
    problems = []
    cap, pidfile = capture(ev, 'host-lo', [args.port, args.peer_port,
                                           args.fs_peer_port])
    try:
        run_diod(args, ev, problems)
        run_peer(args, ev, problems)
        run_fs_peer(args, ev, problems)
    finally:
        stop_capture(cap, pidfile)
    check_wire(ev, problems)
    open(os.path.join(ev, 'verdict.txt'), 'w').write(
        ('FAIL\n' + '\n'.join(problems) if problems else 'PASS') + '\n')
    print('\n'.join(problems) or 'test_9p: PASS')
    return 1 if problems else 0


if __name__ == '__main__':
    sys.exit(main())
