#!/usr/bin/env python3
# Copyright (C) 2026 Greg Burd
#
# This work is open source software, licensed under the terms of the
# BSD license as described in the LICENSE file in the top-level directory.
#
# Summarise a gen-ctype-oracle dump (stdin) as ranges and hashes:
#   class <name> <count> <sha256> then "  <first>-<last>" runs where it holds;
#   toupper/tolower <count of changed cps> <sha256> then "  <first>-<last> <delta>"
#   runs with a constant delta; width <value> <count> <sha256> then runs.
# The sha256 is over the property's value for every code point in order, one
# line each, so two dumps agree on a property exactly when the hashes match.
#   ctype-summary.py --compare A B: per-property counts of differing code points.
import hashlib, sys

NAMES = ['alnum', 'alpha', 'blank', 'cntrl', 'digit', 'graph',
         'lower', 'print', 'punct', 'space', 'upper', 'xdigit']

def load(f):
    rows = []
    for line in f:
        cp, bits, up, lo, w = line.split()
        rows.append((int(bits, 16), int(up, 16), int(lo, 16), int(w)))
    assert len(rows) == 0x110000, len(rows)
    return rows

def runs(key):
    """(first, last, k) for each maximal run of code points with the same
    key(cp) that is not None."""
    out = []
    for cp in range(0x110000):
        k = key(cp)
        if k is None:
            continue
        if out and out[-1][1] == cp - 1 and out[-1][2] == k:
            out[-1] = (out[-1][0], cp, k)
        else:
            out.append((cp, cp, k))
    return out

def digest(vals):
    return hashlib.sha256(''.join('%x\n' % v for v in vals).encode()).hexdigest()

def summary(rows):
    for i, n in enumerate(NAMES):
        col = [(r[0] >> i) & 1 for r in rows]
        rs = runs(lambda cp: 1 if col[cp] else None)
        print('class %s %d %s' % (n, sum(col), digest(col)))
        for a, b, _ in rs:
            print('  %04x-%04x' % (a, b))
    for j, n in ((1, 'toupper'), (2, 'tolower')):
        col = [r[j] for r in rows]
        rs = runs(lambda cp: col[cp] - cp if col[cp] != cp else None)
        print('%s %d %s' % (n, sum(1 for cp in range(0x110000) if col[cp] != cp), digest(col)))
        for a, b, d in rs:
            print('  %04x-%04x %+d' % (a, b, d))
    col = [r[3] for r in rows]
    print('width all %s' % digest([w & 0xffffffff for w in col]))
    for w in sorted(set(col)):
        rs = runs(lambda cp: 1 if col[cp] == w else None)
        print('width %d %d' % (w, sum(1 for v in col if v == w)))
        if w != 1:
            for a, b, _ in rs:
                print('  %04x-%04x' % (a, b))

def compare(a, b):
    for i, n in enumerate(NAMES):
        d = [cp for cp in range(0x110000) if (a[cp][0] ^ b[cp][0]) >> i & 1]
        print('%-8s %6d%s' % (n, len(d), '' if not d else '  e.g. ' + ' '.join('%04x' % c for c in d[:6])))
    for j, n in ((1, 'toupper'), (2, 'tolower'), (3, 'wcwidth')):
        d = [cp for cp in range(0x110000) if a[cp][j] != b[cp][j]]
        print('%-8s %6d%s' % (n, len(d), '' if not d else '  e.g. ' + ' '.join('%04x' % c for c in d[:6])))

if len(sys.argv) == 4 and sys.argv[1] == '--compare':
    compare(load(open(sys.argv[2])), load(open(sys.argv[3])))
else:
    summary(load(sys.stdin))
