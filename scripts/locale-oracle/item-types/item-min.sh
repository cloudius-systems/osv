#!/bin/sh
# Copyright (C) 2026 Greg Burd
#
# This work is open source software, licensed under the terms of the
# BSD license as described in the LICENSE file in the top-level directory.
#
# The fewest items glibc accepts in each category file (measured):
#   item-min.sh <compiled locale dir, e.g. de_DE.utf8> <scratch dir>
# For each category, the file is copied with its item count set to k, for
# every k from 0 to its count, and loaded alone with newlocale(1 << cat);
# prints "min <cat> <k>": the smallest k from which every larger k loads.
set -e
src=$1; tmp=$2; here=$(cd "$(dirname "$0")" && pwd)
mkdir -p "$tmp"
cc -O1 -o "$tmp/can-load" "$here/can-load.c"
for c in 0 1 2 3 4 5 7 8 9 10 11 12; do
    python3 - "$src" "$tmp" "$c" "$tmp/can-load" <<'PY'
import os, shutil, struct, subprocess, sys
src, tmp, c, prog = sys.argv[1], sys.argv[2], int(sys.argv[3]), sys.argv[4]
names = ["LC_CTYPE", "LC_NUMERIC", "LC_TIME", "LC_COLLATE", "LC_MONETARY",
         "LC_MESSAGES/SYS_LC_MESSAGES", None, "LC_PAPER", "LC_NAME", "LC_ADDRESS",
         "LC_TELEPHONE", "LC_MEASUREMENT", "LC_IDENTIFICATION"]
b = bytearray(open(os.path.join(src, names[c]), 'rb').read())
n = struct.unpack_from('<I', b, 4)[0]
ok = []
for k in range(n + 1):
    d = os.path.join(tmp, 'm%d_%d.utf8' % (c, k))
    os.makedirs(os.path.dirname(os.path.join(d, names[c])), exist_ok=True)
    struct.pack_into('<I', b, 4, k)
    open(os.path.join(d, names[c]), 'wb').write(b)
    r = subprocess.run([prog, str(c), os.path.basename(d)], env=dict(os.environ, LOCPATH=tmp),
                       capture_output=True, text=True).stdout.strip()
    ok.append(r == 'ok')
    shutil.rmtree(d)
m = n
while m > 0 and ok[m - 1]:
    m -= 1
print('min %d %d of %d' % (c, m, n))
PY
done
