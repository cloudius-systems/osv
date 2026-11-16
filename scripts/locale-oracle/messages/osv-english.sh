#!/bin/bash
# Copyright (C) 2026 Greg Burd.
#
# This work is open source software, licensed under the terms of the
# BSD license as described in the LICENSE file in the top-level directory.
#
# osv-english.sh <osv tree> <lmp binary> <out dir>: the English messages OSv
# printed before Stage 1d (musl 1.2.1's strerror, strsignal, gai_strerror,
# regerror, built for Linux untranslated) against glibc's (lmp C all), and the
# list of every difference.
O=$1; L=$2; D=$3; H=$(cd "$(dirname "$0")" && pwd); mkdir -p $D; T=$(mktemp -d); trap 'find $T -delete' EXIT
M=$O/musl/src
cat > $T/main.c <<'CC'
#include <stdio.h>
#include <string.h>
#include <regex.h>
char *musl_strerror(int); char *musl_strsignal(int); const char *musl_gai_strerror(int);
size_t musl_regerror(int, const regex_t *, char *, size_t);
static void put(const char *k, long n, const char *s) {
  printf("%s %ld ", k, n);
  for (const unsigned char *p = (const unsigned char *)s; *p; p++) if (*p < 32 || *p == '\\') printf("\\x%02x", *p); else putchar(*p);
  putchar('\n'); }
int main(void) {
  for (int e = -2; e <= 140; e++) put("strerror", e, musl_strerror(e));
  for (int s = -1; s <= 66; s++) put("strsignal", s, musl_strsignal(s));
  for (int g = -110; g <= 1; g++) put("gai_strerror", g, musl_gai_strerror(g));
  for (int r = 0; r <= 16; r++) { char b[256]; musl_regerror(r, 0, b, sizeof b); put("regerror", r, b); }
  return 0; }
CC
F="-I$H/stub -I$M/errno -w -O1"
gcc $F -Dstrerror=musl_strerror -D__strerror_l=musl___strerror_l -Dstrerror_l=musl_strerror_l -c $M/errno/strerror.c -o $T/a.o &&
gcc $F -Dstrsignal=musl_strsignal -c $M/string/strsignal.c -o $T/b.o &&
gcc $F -Dgai_strerror=musl_gai_strerror -c $M/network/gai_strerror.c -o $T/c.o &&
gcc $F -Dregerror=musl_regerror -c $M/regex/regerror.c -o $T/d.o &&
gcc -o $T/m $T/main.c $T/a.o $T/b.o $T/c.o $T/d.o || exit 1
$T/m > $D/osv-before.txt
LANGUAGE= LC_ALL=C $L C all | grep -v '^\(hstrerror\|regerror-len\)' > $D/glibc-C.txt
python3 - $D/osv-before.txt $D/glibc-C.txt > $D/english-diff.txt <<'PY'
import sys
a = dict((tuple(l.split(' ', 2)[:2]), l.rstrip('\n').split(' ', 2)[2] if l.count(' ') >= 2 else '') for l in open(sys.argv[1]))
b = dict((tuple(l.split(' ', 2)[:2]), l.rstrip('\n').split(' ', 2)[2] if l.count(' ') >= 2 else '') for l in open(sys.argv[2]))
n = {}
for k in b:
    if a.get(k) != b[k]:
        n[k[0]] = n.get(k[0], 0) + 1
        print(f"{k[0]} {k[1]}: OSv \"{a.get(k)}\"  glibc \"{b[k]}\"")
tot = {}
for k in b: tot[k[0]] = tot.get(k[0], 0) + 1
print("# differ: " + ", ".join(f"{f} {n.get(f, 0)}/{tot[f]}" for f in tot))
PY
tail -1 $D/english-diff.txt
