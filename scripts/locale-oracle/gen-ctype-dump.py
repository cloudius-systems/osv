#!/usr/bin/env python3
# Copyright (C) 2026 Greg Burd
#
# This work is open source software, licensed under the terms of the
# BSD license as described in the LICENSE file in the top-level directory.
#
# Reconstruct a gen-ctype-oracle dump (per code point: "<cp> <bits> <towupper>
# <towlower> <wcwidth>") from an s-glibc summary (ctype-summary.py output).
# This proves the summary is a lossless build input: the output must be
# byte-identical to the dump it was summarised from.
#   gen-ctype-dump.py s-glibc-C.UTF-8.txt > reconstructed.txt
#   cmp reconstructed.txt d-glibc-C.UTF-8.txt
#
# Width 1 carries no ranges in the summary (it is the complement of -1/0/2
# over 0..0x10ffff); every other property is given as explicit ranges.
import sys

NAMES = ['alnum', 'alpha', 'blank', 'cntrl', 'digit', 'graph',
         'lower', 'print', 'punct', 'space', 'upper', 'xdigit']
NCP = 0x110000


def parse(path):
    bits = [0] * NCP
    toup = list(range(NCP))
    tolo = list(range(NCP))
    width = [1] * NCP  # default; -1/0/2 ranges overwrite below
    cur = None  # ('class', i) | ('toupper',) | ('tolower',) | ('width', w)
    for line in open(path):
        if line.startswith('  '):
            f = line.split()
            lo, hi = (int(x, 16) for x in f[0].split('-'))
            if cur[0] == 'class':
                i = cur[1]
                for cp in range(lo, hi + 1):
                    bits[cp] |= 1 << i
            elif cur[0] == 'toupper':
                d = int(f[1])
                for cp in range(lo, hi + 1):
                    toup[cp] = cp + d
            elif cur[0] == 'tolower':
                d = int(f[1])
                for cp in range(lo, hi + 1):
                    tolo[cp] = cp + d
            elif cur[0] == 'width':
                w = cur[1]
                for cp in range(lo, hi + 1):
                    width[cp] = w
            continue
        h = line.split()
        if h[0] == 'class':
            cur = ('class', NAMES.index(h[1]))
        elif h[0] == 'toupper':
            cur = ('toupper',)
        elif h[0] == 'tolower':
            cur = ('tolower',)
        elif h[0] == 'width':
            cur = ('width', None) if h[1] == 'all' else ('width', int(h[1]))
        else:
            sys.exit('unexpected line: ' + line)
    return bits, toup, tolo, width


def main():
    if len(sys.argv) != 2:
        sys.exit('usage: gen-ctype-dump.py <s-glibc summary>')
    bits, toup, tolo, width = parse(sys.argv[1])
    out = sys.stdout.write
    for cp in range(NCP):
        out('%x %x %x %x %d\n' % (cp, bits[cp], toup[cp], tolo[cp], width[cp]))


if __name__ == '__main__':
    main()
