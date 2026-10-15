#!/usr/bin/env python3
# Copyright (C) 2026 Greg Burd.
#
# This work is open source software, licensed under the terms of the
# BSD license as described in the LICENSE file in the top-level directory.
#
# mkmo.py <out.mo> <variant> [k=v...]: hand-made .mo files from the public
# format description (magic, revision, N, O, T, S, H; tables of (length,
# offset); NUL-terminated strings; optional hash table). Variants:
#   plain      sorted, no hash        unsorted   originals out of order, no hash
#   hash       sorted + hash table    unsortedh  out of order + hash table
#   emptyhash  sorted + hash table of zeros
#   emptytr    one translation of length 0     rev    revision=<rev>
#   big        big-endian, sorted, hash
#   rev        revision=<rev> [hsize=<n>] (minor 1: 12-word header, no system-dependent strings)
#   pad        <pad> bytes between header and tables (misaligned tables when pad % 4)
#   charset    header charset=<cs>; "u" -> U+00FC U+20AC in UTF-8 bytes
#   nocharset  header without Content-Type    noheader  no "" entry at all
import struct, sys
def hashpjw(s):
    h = 0
    for c in s:
        h = ((h << 4) + c) & 0xffffffff
        g = h & 0xf0000000
        if g: h ^= g >> 24; h ^= g
    return h
def build(entries, order=None, hsize=0, endian='<', rev=0, zero_hash=False, pad=0):
    order = order or sorted(range(len(entries)), key=lambda i: entries[i][0])
    ents = [entries[i] for i in order]
    hw = 48 if rev & 0xffff else 28
    n = len(ents); O = hw + pad; T = O + 8 * n; H = T + 8 * n
    data = b''; offs_o = []; offs_t = []; base = H + 4 * hsize
    for k, v in ents:
        offs_o.append((len(k), base + len(data))); data += k + b'\0'
    for k, v in ents:
        offs_t.append((len(v), base + len(data))); data += v + b'\0'
    tab = [0] * hsize
    assert not hsize or zero_hash or hsize > len(ents) + 1, "hash table too small"
    if hsize and not zero_hash:
        for i, (k, v) in enumerate(ents):
            h = hashpjw(k); idx = h % hsize; inc = 1 + h % (hsize - 2)
            while tab[idx]: idx = (idx + inc) % hsize
            tab[idx] = i + 1
    f = endian
    out = struct.pack(f + '7I', 0x950412de, rev, n, O, T, hsize, H)
    if hw == 48: out += struct.pack(f + '5I', 0, 0, 0, 0, 0)
    out += b'\0' * pad
    for l, o in offs_o: out += struct.pack(f + '2I', l, o)
    for l, o in offs_t: out += struct.pack(f + '2I', l, o)
    for x in tab: out += struct.pack(f + 'I', x)
    return out + data
hdr = b'Content-Type: text/plain; charset=UTF-8\nPlural-Forms: nplurals=2; plural=n != 1;\n'
E = [(b'', hdr), (b'alpha', b'A'), (b'beta', b'B'), (b'gamma', b'G'), (b'delta', b'D'),
     (b'pl', b'P0\0P1'), (b'zeta', b'Z')]
out, var = sys.argv[1], sys.argv[2]
kw = dict(a.split('=') for a in sys.argv[3:])
if var == 'plain': b = build(E)
elif var == 'hash': b = build(E, hsize=11)
elif var == 'big': b = build(E, hsize=11, endian='>')
elif var == 'unsorted': b = build(E, order=[0, 3, 1, 6, 2, 5, 4])
elif var == 'unsortedh': b = build(E, order=[0, 3, 1, 6, 2, 5, 4], hsize=11)
elif var == 'emptyhash': b = build(E, hsize=11, zero_hash=True)
elif var == 'emptytr': b = build(E + [(b'empty', b'')], hsize=11)
elif var == 'rev': b = build(E, rev=int(kw['rev'], 0), hsize=int(kw.get('hsize', '0')))
elif var == 'pad': b = build(E, pad=int(kw['pad']), hsize=int(kw.get('hsize', '0')))
elif var == 'charset': b = build([(b'', ('Content-Type: text/plain; charset=%s\n' % kw['cs']).encode())] + E[1:] + [(b'u', b'\xc3\xbc\xe2\x82\xac')])
elif var == 'nocharset': b = build([(b'', b'Plural-Forms: nplurals=2; plural=n != 1;\n')] + E[1:] + [(b'u', b'\xc3\xbc\xe2\x82\xac')])
elif var == 'noheader': b = build(E[1:] + [(b'u', b'\xc3\xbc\xe2\x82\xac')])
open(out, 'wb').write(b)
