#!/usr/bin/env python3
# Copyright (C) 2026 Greg Burd
#
# This work is open source software, licensed under the terms of the
# BSD license as described in the LICENSE file in the top-level directory.
#
# Derive the item types, the fewest items and the codeset item of each
# category from measurements of glibc (no glibc source read), and compare
# them with libc/locale/locale-data.cc and libc/locale/langinfo-builtin.inc:
#   item-types.py <dump> <min> <locale-data.cc> [<langinfo-builtin.inc> [<names>]]
# <dump> is item-dump's output for C, C.UTF-8, xx_L1 (an ISO-8859-1 locale)
# and UTF-8 locales in directory form (names ending .utf8, as localedef
# writes them; LOCPATH): item-types.sh runs it over the test locales and
# every UTF-8 locale the host's localedef can compile. <min> is
# item-min.sh's output.
# Exit 1 on any mismatch.
#
# Types, measured per item over C, C.UTF-8, xx_L1 (ISO-8859-1) and every
# UTF-8 locale given. For a locale read from files, the span of an item is
# the distance from its offset to the next larger offset in the file's own
# index (the header: magic, count, then one offset per item), or to the end
# of the file. In one such locale an item can be:
#   d  nl_langinfo_l's value is not the address of the item's offset in its
#      own file, and its low 32 bits are the word stored there (glibc returns
#      a word in the pointer; the high bits are not always 0)
#   r  a pointer, 4-aligned, span 8, second word not 0: two words
#   w  a pointer, 4-aligned, span a multiple of 4, every word a code point
#      (<= 0x10ffff), and the last word 0 or the span one word: a wide
#      string, a list of them, or one wide character
#   s  a pointer whose span ends in a NUL and is valid UTF-8 (in xx_L1, any
#      bytes): a string, or a list of strings (ERA, ALT_DIGITS, class names)
#   b  a pointer to one byte: span 1, or span up to 4 holding a control
#      character (< 0x20) or 0xff, then NUL padding
#   x  any pointer
# C is built into glibc (its pointers are into libc.so.6): no span, at most
# 512 bytes read; d a word, w aligned code points to a 0 word, s a NUL, b a
# NUL in the first two bytes, r aligned.
# An item's type is the first of d r w b s that it can be in every locale
# (b only if non-zero in some locale: an empty string is s); x if none (a
# word in some locales and a pointer in others, or an internal table). Ties
# are printed as "ambiguous" with the candidates.
# langinfo-builtin.inc (#1477) holds C's and C.UTF-8's values; its S and B
# both become strings in locale.cc, so either is accepted where s or b can
# be; it is checked against both locales.
import os, re, struct, sys

dump, minf, ldcc = sys.argv[1:4]
inc = sys.argv[4] if len(sys.argv) > 4 else None
names = {}
if len(sys.argv) > 5:
    for line in open(sys.argv[5]):
        c, i, n_ = line.split()
        names.setdefault((int(c), int(i)), n_)
def nm(c, i):
    return names.get((c, i), '-')
CATS = [c for c in range(13) if c != 6]
rows = {}   # (loc, cat) -> {idx: (kind, path, off, bytes) | ('W', value)}
past = {}
for line in open(dump):
    p = line.split()
    if len(p) < 3 or p[1] == 'NOLOCALE':
        continue
    loc, cat = p[0], int(p[1])
    if p[2] == 'E':
        past[(loc, cat)] = int(p[3], 16)
        continue
    idx, kind = int(p[2]), p[3]
    if kind == 'W':
        rows.setdefault((loc, cat), {})[idx] = ('W', int(p[4]))
    else:
        b = bytes.fromhex(p[7]) if len(p) > 7 and p[7] != 'PAST' else b''
        rows.setdefault((loc, cat), {})[idx] = ('P', p[4], int(p[5]), b, 'PAST' in p, int(p[6], 16))

locs = sorted({l for (l, c) in rows})
files = [l for l in locs if l.endswith('.utf8')]
fails = 0
inc_fails = []

def nitems(loc, cat):
    r = rows[(loc, cat)]
    n = 0
    for i, v in r.items():
        if not (v[0] == 'P' and v[4]):
            n = max(n, i + 1)
    return n

FILES = ["LC_CTYPE", "LC_NUMERIC", "LC_TIME", "LC_COLLATE", "LC_MONETARY",
         "LC_MESSAGES/SYS_LC_MESSAGES", None, "LC_PAPER", "LC_NAME", "LC_ADDRESS",
         "LC_TELEPHONE", "LC_MEASUREMENT", "LC_IDENTIFICATION"]

def file_index(loc, cat):
    r = rows[(loc, cat)]
    path = next(v[1] for v in r.values() if v[0] == 'P' and not v[4] and v[1].endswith('/' + FILES[cat]))
    data = open(path, 'rb').read()
    n = struct.unpack_from('<I', data, 4)[0]
    return data, [struct.unpack_from('<I', data, 8 + 4 * i)[0] for i in range(n)]

def span_of(loc, cat, i):
    data, offs = idx_cache[(loc, cat)]
    o = offs[i]
    nxt = [x for x in offs if x > o]
    return data, o, (nxt and min(nxt) or len(data)) - o

def utf8_ok(b):
    try:
        b.decode('utf-8', 'strict')
        return True
    except UnicodeDecodeError:
        return False

def wide_to_zero(data, o, limit):
    """From offset o: words that are code points up to a 0 word, all before
    limit (the validator's wide-string check, with code points)."""
    if o % 4:
        return False
    for j in range(o, limit - 3, 4):
        w = struct.unpack_from('<I', data, j)[0]
        if w == 0:
            return True
        if w > 0x10ffff:
            return False
    return False

def kinds_of(loc, cat, i):
    v = rows[(loc, cat)][i]
    if loc == 'C':
        return c_kinds(v)
    data, o, sp = span_of(loc, cat, i)
    # A pointer into the item's own file at its own offset; anything else is
    # a word if its low 32 bits are the word at the offset.
    own = v[0] == 'P' and v[1].endswith('/' + FILES[cat]) and v[2] == o
    if not own:
        val = v[1] if v[0] == 'W' else v[5]
        return {'d'} if o + 4 <= len(data) and val & 0xffffffff == struct.unpack_from('<I', data, o)[0] else {'!'}
    b = data[o:o + sp]
    k = {'x'}
    words = [struct.unpack_from('<I', b, j)[0] for j in range(0, sp - 3, 4)]
    if o % 4 == 0 and sp == 8 and words[1] != 0:
        k.add('r')
    if o % 4 == 0 and sp % 4 == 0 and words and all(w <= 0x10ffff for w in words) and \
       (words[-1] == 0 or sp == 4):
        k.add('w')
    # xx_L1's strings are ISO-8859-1: any bytes ending in a NUL.
    if b.endswith(b'\0') and (loc.startswith('xx_L1') or utf8_ok(b)):
        k.add('s')
    # b: one byte, then NUL padding (a control character, or 0xff: "not
    # given"); one byte with no padding, any value.
    if 1 <= sp <= 4 and not b[1:].strip(b'\0') and (sp == 1 or b[0] < 0x20 or b[0] == 0xff):
        k.add('b0' if b[0] == 0 else 'b')
    return k

def c_kinds(v):
    """C, built into glibc: no span; read at most 512 bytes."""
    if v[0] == 'W' or 'libc.so' not in v[1]:
        return {'d'}
    b, off = v[3], v[2]
    k = {'x'}
    if off % 4 == 0 and len(b) >= 8:
        k.add('r')
    for j in range(0, len(b) - 3, 4):
        w = struct.unpack_from('<I', b, j)[0]
        if w > 0x10ffff:
            break
        if w == 0 or j == 0 and off % 4 == 0 and b[4:8] == b'\0\0\0\0':
            if off % 4 == 0:
                k.add('w')
            break
    z = b.find(b'\0')
    if z >= 0:
        k.add('s')
        if z <= 1:
            k.add('b0' if z == 0 else 'b')
    return k

ambiguous = []

def file_type(cat, i, locales):
    ks = [kinds_of(l, cat, i) for l in locales]
    # b: a byte in every locale (b or b0), non-zero in at least one.
    if all('b' in k or 'b0' in k for k in ks) and any('b' in k for k in ks):
        ks = [k | {'b'} for k in ks]
    common = set.intersection(*ks)
    order = [t for t in 'drwbs' if t in common]
    if len(order) > 1:
        ambiguous.append((cat, i, ''.join(order)))
    return order[0] if order else 'x'

filelocs = files + [l for l in locs if l.startswith('xx_L1') or l in ('C', 'C.UTF-8')]
idx_cache = {(l, c): file_index(l, c) for l in filelocs if l != 'C' for c in CATS}
measured = {}
for c in CATS:
    n = min(nitems(l, c) for l in filelocs if l != 'C')
    measured[c] = ''.join(file_type(c, i, filelocs) for i in range(n))
    print('measured %2d %3d %s' % (c, n, measured[c]))

for c, i, ts in ambiguous:
    print('ambiguous %2d %3d (%s): %s in every locale; the first is taken' % (c, i, nm(c, i), ts))

# The table in locale-data.cc: an index past its string is x.
src = open(ldcc).read()
m = re.search(r'item_types\[ncat\] = \{(.*?)\};', src, re.S)
table = []
for part in re.split(r',\s*\n', m.group(1).strip()):
    part = part.strip().rstrip(',')
    table.append(None if part == 'nullptr' else ''.join(re.findall(r'"([^"]*)"', part)))
for c in CATS:
    t = table[c]
    a, b = measured[c], t + 'x' * (len(measured[c]) - len(t))
    diff = [i for i in range(len(a)) if a[i] != b[i]]
    print('table    %2d %s %s' % (c, 'EQUAL' if not diff else 'DIFFER (measured/table) at ' + ' '.join(
        '%d:%s/%s(%s)' % (i, a[i], b[i], nm(c, i)) for i in diff), '(+%d x)' % (len(a) - len(t)) if len(a) > len(t) else ''))
    fails += bool(diff)

# min_items and codeset_item.
mins = {}
for line in open(minf):
    p = line.split()
    if p and p[0] == 'min':
        mins[int(p[1])] = int(p[2])
mt = [int(x) for x in re.search(r'min_items\[ncat\] = \{([^}]*)\}', src).group(1).split(',')]
ct = [int(x) for x in re.search(r'codeset_item\[ncat\] = \{([^}]*)\}', src).group(1).split(',')]
for c in CATS:
    ok = mins.get(c) == mt[c]
    print('min      %2d measured %s table %d %s' % (c, mins.get(c), mt[c], 'EQUAL' if ok else 'DIFFER'))
    fails += not ok
latin = [l for l in locs if l.startswith('xx_L1')]
for c in CATS:
    cands = []
    for i in range(len(measured[c])):
        if measured[c][i] != 's':
            continue
        if all(rows[(l, c)][i][3].split(b'\0')[0] == b'UTF-8' for l in files + ['C.UTF-8']) and \
           all(rows[(l, c)][i][3].split(b'\0')[0] == b'ISO-8859-1' for l in latin):
            cands.append(i)
    ok = cands == [ct[c]]
    print('codeset  %2d measured %s table %d %s' % (c, cands, ct[c], 'EQUAL' if ok else 'DIFFER'))
    fails += not ok

# #1477's table of C's and C.UTF-8's values, item by item, against what each
# value can be in that locale (kinds_of).
def can_be(loc, c, i, t):
    k = kinds_of(loc, c, i)
    return t in k or (t in 'sb' and ({'s', 'b', 'b0'} & k))

itypes = {}
if inc:
    cur = None
    for line in open(inc):
        mm = re.match(r'static const struct item cat(\d+)_c', line)
        if mm:
            cur = int(mm.group(1))
            itypes[cur] = []
            continue
        mm = re.match(r'\s*/\*\s*(\d+) (\S+)\s*\*/ (S|B|D|W|RATE)\(', line)
        if mm and cur is not None:
            itypes[cur].append({'S': 's', 'B': 'b', 'D': 'd', 'W': 'w', 'RATE': 'r'}[mm.group(3)])
        if line.startswith('};'):
            cur = None
for c in CATS:
    n = len(measured[c])
    if c in itypes:
        t = ''.join(itypes[c])
        for loc in ('C', 'C.UTF-8'):
            diff = [i for i in range(min(len(t), n)) if not (can_be(loc, c, i, t[i]) or
                    (t[i] in 'sb' and can_be(loc, c, i, 's' if t[i] == 'b' else 'b')))]
            print('1477-inc %2d %-7s %3d items %s%s' % (c, loc, len(t), 'AGREE' if not diff else 'DIFFER at ' + ' '.join(
                '%d:inc %s measured %s(%s)' % (i, t[i], measured[c][i], nm(c, i)) for i in diff),
                '' if len(t) == n else ' LENGTH %d vs %d' % (len(t), n)))
            inc_fails.extend((c, loc, i, t[i]) for i in diff)
            if len(t) != n:
                inc_fails.append((c, loc, 'length', len(t)))
        lab = [i for i in range(min(len(t), n)) if t[i] != measured[c][i]]
        if lab:
            print('1477-inc %2d labels that differ from the measured file types (not errors when both are '
                  'NUL-terminated): %s' % (c, ' '.join('%d:inc %s files %s(%s)' % (i, t[i], measured[c][i], nm(c, i)) for i in lab)))
print('SUMMARY %d mismatches with locale-data.cc, %d with langinfo-builtin.inc' % (fails, len(inc_fails)))
sys.exit(1 if fails or inc_fails else 0)
