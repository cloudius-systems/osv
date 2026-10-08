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
#    Rlerror to the root Tgetattr), then lookup, open, read and readdir
#    faults on a small served tree; it reports any fid left unclunked and
#    any Tread larger than the iounit it gave.
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
         24: 'Tgetattr', 25: 'Rgetattr', 40: 'Treaddir', 41: 'Rreaddir',
         117: 'Rread', 118: 'Twrite', 119: 'Rwrite', 120: 'Tclunk',
         121: 'Rclunk'}
HELLO = b'hello from the 9p export\n'
WRITTEN = b'written by OSv over 9P/TCP\n'
DIOD_PASS = ['diod negotiate', 'diod attach', 'diod ENOENT', 'diod read',
             'diod write', 'diod flush', 'diod clunk and shutdown']
PEER_PASS = ['peer fragmentation and coalescing',
             'peer partial frame then EOF', 'peer stall mid-frame',
             'peer reset']
LINKED = b'one object, two names\n'
DIR_ENTRIES = 400
FS_DIOD_PASS = ['diod mount and unmount', 'diod mounts are distinct',
                'diod refusals', 'diod read files',
                'diod read across iounit chunks', 'diod hardlink identity',
                '(w) write unsupported',
                'diod readdir across Rreaddir replies', 'diod create',
                'diod namespace changes', 'diod chmod', 'diod statfs',
                'diod replacement while open']
OLD_OBJ, NEW_OBJ = b'old object\n', b'new object\n'
FS_PEER_PASS = ['peer bad aname', 'peer wrong version',
                'peer root getattr error', 'peer lookup getattr error',
                'peer lookup qid mismatch', 'peer lookup unknown type',
                'peer lookup reused identity', 'peer lopen error',
                'peer lopen qid mismatch', 'peer special file',
                'peer iounit', 'peer read error after progress',
                'peer readdir short record', 'peer readdir slash in name',
                'peer readdir cookie past off_t', 'peer lcreate error',
                'peer lcreate replaced', 'peer mkdir error',
                'peer unlink error', 'peer rename error',
                'peer rename fallback error', 'peer unlink fallback error',
                'peer setattr error', 'peer statfs error']
DIOD_WIRE = ['Tversion', 'Rversion', 'Tattach', 'Rattach', 'Twalk', 'Rwalk',
             'Tlopen', 'Rlopen', 'Tread', 'Rread', 'Twrite', 'Rwrite',
             'Tclunk', 'Rclunk', 'Tflush', 'Rflush', 'Rlerror', 'Tgetattr',
             'Rgetattr', 'Treaddir', 'Rreaddir']


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


# The fault peer for tst-9p-fs, one connection per case, in the order
# tst-9p-fs runs them: three mount failures, then faults on a tree of a
# root directory (qid path 1), f (path 2, 1000 bytes, byte i is i % 251)
# and p (path 3, a FIFO). Fids never clunked are reported as 'unclunked'
# lines, protocol misuse as 'BAD' lines.
FS_CASES = ['aname', 'version', 'getattr', 'lookup-getattr', 'lookup-qid',
            'lookup-type', 'live-reuse', 'lopen', 'lopen-qid', 'special',
            'iounit', 'read-error', 'rd-short', 'rd-slash', 'rd-cookie',
            'lcreate', 'lcreate-qid', 'mkdir', 'unlink', 'rename',
            'rename-old', 'unlink-old', 'setattr', 'statfs']
# Rlerror answers per case: (request type, errno).
FS_ERR = {'mkdir': (72, 13), 'unlink': (76, 13), 'rename': (74, 18),
          'rename-old': (20, 18), 'unlink-old': (122, 13),
          'setattr': (26, 1), 'statfs': (8, 38)}
# object: (mode, size). u has no file type; g is a directory whose qid
# path is f's, as if the server reused a live identity.
FS_TREE = {1: (0o40755, 0), 2: (0o100644, 1000), 3: (0o10644, 0),
           4: (0o644, 0), 5: (0o40755, 0), 6: (0o100600, 0)}
FS_NAMES = {'f': 2, 'p': 3, 'u': 4, 'g': 5}
FS_QPATH = {5: 2}
FS_DATA = bytes(i % 251 for i in range(1000))
EACCES, EIO, ENOENT, EOPNOTSUPP = 13, 5, 2, 95


def qid(obj, qpath=None):
    t = 0x80 if FS_TREE[obj][0] & 0o170000 == 0o40000 else 0
    return struct.pack('<BIQ', t, 0, qpath or FS_QPATH.get(obj, obj))


def rgetattr(obj, qpath=None):
    mode, size = FS_TREE[obj]
    return (struct.pack('<Q', 0x7ff) + qid(obj, qpath)
            + struct.pack('<IIIQQQQQ', mode, 1000, 1000, 1, 0, size, 4096,
                          (size + 511) // 512) + bytes(64 + 16))


def dirent(path, cookie, name):
    return (qid(path) + struct.pack('<QB', cookie, 4 if path == 1 else 8)
            + struct.pack('<H', len(name)) + name)


def rreaddir(case, off):
    if off:
        return b''   # one batch, then end of directory
    if case == 'rd-short':   # name[s] says 40 bytes, one follows
        return dirent(2, 1, b'f')[:22] + struct.pack('<H', 40) + b'f'
    if case == 'rd-slash':
        return dirent(2, 1, b'a/b')
    if case == 'rd-cookie':
        return dirent(2, 1 << 63, b'f')
    return dirent(1, 1, b'.') + dirent(2, 2, b'f')


def fs_peer_answer(case, typ, body, fids, log):
    """The reply to one request, or None to say nothing."""
    u32 = lambda at: struct.unpack('<I', body[at:at + 4])[0]
    err = lambda e: (7, struct.pack('<I', e))
    if typ == 104:
        if case == 'aname':
            return err(ENOENT)
        fids[u32(0)] = 1
        return 105, qid(1)
    if typ in (120, 122):   # Tclunk, Tremove: the fid is gone either way
        obj = fids.pop(u32(0), None)
        if typ == 122 and case in FS_ERR and FS_ERR[case][0] == 122:
            if obj != 2:
                log.append('fs-%s: BAD Tremove of fid %d, not f'
                           % (case, u32(0)))
            return err(FS_ERR[case][1])
        return typ + 1, b''
    if u32(0) not in fids:
        log.append('fs-%s: BAD %s on unknown fid %d' % (
            case, NAMES.get(typ, typ), u32(0)))
        return err(EIO)
    obj = fids[u32(0)]
    if typ == 24:
        if case == 'getattr' or (case == 'lookup-getattr' and obj != 1):
            return err(EACCES)
        bad = case == 'lookup-qid' and obj != 1
        return 25, rgetattr(obj, 99 if bad else None)
    if typ == 110:   # fid newfid nwname names
        n = struct.unpack('<H', body[8:10])[0]
        if n == 0:
            fids[u32(4)] = obj
            return 111, struct.pack('<H', 0)
        name = body[12:12 + struct.unpack('<H', body[10:12])[0]]
        if (n == 1 and obj == 1 and name == b'n' and case == 'lcreate-qid'
                and 6 in fids.values()):
            fids[u32(4)] = 2   # after Rlcreate: another object, f
            return 111, struct.pack('<H', 1) + qid(2)
        if n != 1 or obj != 1 or name.decode() not in FS_NAMES:
            return err(ENOENT)
        fids[u32(4)] = FS_NAMES[name.decode()]
        return 111, struct.pack('<H', 1) + qid(fids[u32(4)])
    if typ == 12:    # Tlopen fid flags -> qid iounit
        if case == 'lopen' and obj != 1:
            return err(EACCES)
        q = qid(obj, 99 if case == 'lopen-qid' and obj != 1 else None)
        iounit = 100 if case in ('iounit', 'read-error') else 0
        return 13, q + struct.pack('<I', iounit)
    if typ == 116:   # Tread fid offset count
        off, count = struct.unpack('<QI', body[4:16])
        if case in ('iounit', 'read-error') and count > 100:
            log.append('fs-%s: BAD Tread count %d above iounit 100'
                       % (case, count))
        if case == 'read-error' and off > 0:
            return err(EIO)
        lim = 100 if case in ('iounit', 'read-error') else count
        if case == 'iounit' and off == 0:
            lim = 60   # a short count that is not EOF
        d = FS_DATA[off:off + min(count, lim)]
        return 117, struct.pack('<I', len(d)) + d
    if typ == 14:    # Tlcreate fid name flags mode gid -> qid iounit
        at = 6 + struct.unpack('<H', body[4:6])[0]
        flags, mode, gid = struct.unpack('<III', body[at:at + 12])
        if flags & 0o300 != 0o300 or mode != 0o600 or gid != 1000:
            log.append('fs-%s: BAD Tlcreate flags %o mode %o gid %d'
                       % (case, flags, mode, gid))
        if case == 'lcreate':
            return err(EACCES)
        fids[u32(0)] = 6
        return 15, qid(6) + struct.pack('<I', 0)
    if case in ('rename-old', 'unlink-old') and typ in (74, 76):
        return err(EOPNOTSUPP)
    if typ == 20 and obj != 2:   # Trename fid dfid name: fid must be f's
        log.append('fs-%s: BAD Trename of fid %d, not f' % (case, u32(0)))
    if case in FS_ERR and FS_ERR[case][0] == typ:
        return err(FS_ERR[case][1])
    if typ == 40:    # Treaddir fid offset count
        off, count = struct.unpack('<QI', body[4:16])
        d = rreaddir(case, off)[:count]
        return 41, struct.pack('<I', len(d)) + d
    return err(EOPNOTSUPP)


def fs_peer_conn(c, case, log):
    fids = {}
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
            continue
        r = fs_peer_answer(case, typ, body, fids, log)
        c.sendall(reply(r[0], tag, r[1]))
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
    # No VNC display, so runs with distinct ports can share a host.
    run = ['timeout', '300', sys.executable, 'scripts/run.py', '-m', '2G',
           '-c', '2', '--arch', args.arch, '--nogdb', '--novnc', '--pass-args',
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
    open(os.path.join(a, 'h1'), 'wb').write(LINKED)
    open(os.path.join(a, 'trunc'), 'wb').write(b'some bytes\n')
    open(os.path.join(a, 'modef'), 'wb').close()
    os.chmod(os.path.join(a, 'modef'), 0o644)
    open(os.path.join(a, 'victim'), 'wb').write(OLD_OBJ)
    open(os.path.join(root, 'victim.new'), 'wb').write(NEW_OBJ)
    sv = os.statvfs(a)
    open(os.path.join(a, 'statfs.txt'), 'w').write('%d %d %d\n' % (
        sv.f_bsize, sv.f_blocks, sv.f_namemax))
    os.mkdir(os.path.join(a, 'd'), 0o700)
    for i in range(DIR_ENTRIES):
        open(os.path.join(a, 'd', 'e%03d' % i), 'wb').close()
    os.link(os.path.join(a, 'h1'), os.path.join(a, 'd', 'h2'))
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
        stop = threading.Event()
        rt = threading.Thread(target=replacer, args=(a, root, stop))
        rt.start()
        fs_console, _ = guest(args, ev, 'fs-diod', 'diod %s %d %d %s' % (
            GUEST_HOST, args.port, os.getuid(), a), 'tst-9p-fs')
        stop.set()
        rt.join(timeout=30)
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
    # tst-9p-tcp wrote it; the (w) group of tst-9p-fs must not change it.
    if open(os.path.join(a, 'out'), 'rb').read() != WRITTEN:
        problems.append('diod: host file a/out does not hold the Twrite bytes')
    problems += ['fs-diod: ' + p for p in host_changes(a)]
    # The guest clunks every fid it creates; diod lists any it still holds.
    leaks = [l for l in open(os.path.join(ev, 'diod.log'))
             if 'unclunked' in l]
    problems += ['diod: server reports ' + l.strip() for l in leaks]


# Once the guest creates a/ready (holding a/victim open), replace a/victim
# by a rename from outside the export and create a/replaced.
def replacer(a, root, stop):
    while not stop.is_set():
        if os.path.exists(os.path.join(a, 'ready')):
            os.rename(os.path.join(root, 'victim.new'),
                      os.path.join(a, 'victim'))
            open(os.path.join(a, 'replaced'), 'wb').close()
            return
        time.sleep(0.05)


# What tst-9p-fs changes left in the export, seen from the host.
def host_changes(a):
    bad = []
    j = lambda n: os.path.join(a, n)
    def mode(n):
        return os.stat(j(n)).st_mode & 0o7777 if os.path.exists(j(n)) else None
    def data(n):
        return open(j(n), 'rb').read() if os.path.exists(j(n)) else None
    if mode('kept') != 0o640 or data('kept') != b'':
        bad.append('host: kept is not an empty 0640 file')
    if mode('modef') != 0o600:
        bad.append('host: modef mode is %s, want 0600' % oct(mode('modef') or 0))
    if data('trunc') != b'':
        bad.append('host: trunc was not truncated')
    if data('victim') != NEW_OBJ:
        bad.append('host: victim is not the replacement')
    bad += ['host: %s still exists' % n for n in ('moved', 'nd')
            if os.path.lexists(j(n))]
    return bad


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


# Every case connected, was closed by the client, left no fid and kept to
# the protocol.
def fs_peer_log(log):
    bad = ['fs-peer: ' + l for l in log if 'unclunked' in l or 'BAD' in l]
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
    assert fs_peer_log(closed + ['fs-iounit: BAD Tread count 1000'])
    fids, log = {}, []
    assert fs_peer_answer('iounit', 104, struct.pack('<I', 5), fids, log)[0] \
        == 105
    w = fs_peer_answer('iounit', 110, struct.pack('<IIHH', 5, 6, 1, 1) + b'f',
                       fids, log)
    assert w[0] == 111 and w[1][2 + 5:2 + 13] == struct.pack('<Q', 2)
    assert fs_peer_answer('iounit', 12, struct.pack('<II', 6, 0), fids,
                          log)[1][13:] == struct.pack('<I', 100)
    r = fs_peer_answer('iounit', 116, struct.pack('<IQI', 6, 900, 1000),
                       fids, log)
    assert r[1] == struct.pack('<I', 100) + FS_DATA[900:] and log
    assert fs_peer_answer('iounit', 116, struct.pack('<IQI', 6, 0, 100),
                          fids, [])[1][:4] == struct.pack('<I', 60)
    assert len(rgetattr(2)) == 153
    assert fs_peer_answer('lookup-qid', 24, struct.pack('<IQ', 6, 0x7ff),
                          fids, [])[1][13:21] == struct.pack('<Q', 99)
    assert fs_peer_answer('iounit', 116, struct.pack('<IQI', 9, 0, 1), fids,
                          [])[0] == 7
    assert qid(5) == struct.pack('<BIQ', 0x80, 0, 2) and qid(2)[0] == 0
    assert rgetattr(5)[8:21] == qid(5)
    assert len(rreaddir('x', 0)) == 2 * 24 + 2 and rreaddir('x', 5) == b''
    fids = {5: 1}
    lc = struct.pack('<IH', 5, 1) + b'n' + struct.pack('<III', 0o301, 0o600,
                                                      1000)
    log = []
    assert fs_peer_answer('lcreate', 14, lc, fids, log)[0] == 7 \
        and fids == {5: 1} and log == []
    assert fs_peer_answer('lcreate-qid', 14, lc.replace(
        struct.pack('<I', 0o301), struct.pack('<I', 0o101)), fids,
        log)[0] == 15 and fids == {5: 6} and log
    fids[7] = 1
    w = fs_peer_answer('lcreate-qid', 110,
                       struct.pack('<IIHH', 7, 8, 1, 1) + b'n', fids, [])
    assert w[0] == 111 and fids[8] == 2
    assert fs_peer_answer('rename', 74, struct.pack('<I', 7), fids,
                          [])[1] == struct.pack('<I', 18)
    assert fs_peer_answer('mkdir', 74, struct.pack('<I', 7), fids,
                          [])[1] == struct.pack('<I', 95)
    fids[9] = 2
    log = []
    assert fs_peer_answer('rename-old', 74, struct.pack('<I', 7), fids,
                          log)[1] == struct.pack('<I', 95)
    assert fs_peer_answer('rename-old', 20, struct.pack('<II', 9, 7), fids,
                          log)[1] == struct.pack('<I', 18) and log == []
    fs_peer_answer('rename-old', 20, struct.pack('<II', 7, 7), fids, log)
    assert log
    log = []
    assert fs_peer_answer('unlink-old', 76, struct.pack('<I', 7), fids,
                          log)[1] == struct.pack('<I', 95)
    assert fs_peer_answer('unlink-old', 122, struct.pack('<I', 9), fids,
                          log)[1] == struct.pack('<I', 13) and 9 not in fids
    assert log == []
    with tempfile.TemporaryDirectory() as t:
        for n, b in (('kept', b''), ('modef', b''), ('trunc', b''),
                     ('victim', NEW_OBJ)):
            open(os.path.join(t, n), 'wb').write(b)
        os.chmod(os.path.join(t, 'kept'), 0o640)
        os.chmod(os.path.join(t, 'modef'), 0o600)
        assert host_changes(t) == []
        os.mkdir(os.path.join(t, 'nd'))
        assert host_changes(t) == ['host: nd still exists']
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
