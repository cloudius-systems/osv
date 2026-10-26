#!/bin/sh
# Copyright (C) 2026 Greg Burd
#
# This work is open source software, licensed under the terms of the
# BSD license as described in the LICENSE file in the top-level directory.
#
# LC_COLLATE sources for rule combinations no shipped locale has, compiled with
# localedef -c (it warns; the output is complete): make-custom-locales.sh <out>
# writes <out>/<name>.utf8 for
#   bp  forward;backward,position     pp  forward,position;position
#   b1  backward;forward              l1  one level
#   f2  forward;forward               p2  forward;forward,position
#   f3  three forward levels          p4  forward;backward;forward;forward,position
#   mx  a backward,position section next to a forward one (glibc's strcoll is
#       not antisymmetric there; a known difference, STAGE1-FORMAT-SPEC.md 6.5)
# coll-diff then runs on them with LOCPATH=<out>.
set -e
out=$1
mkdir -p "$out/src"
hdr() {
    printf 'LC_IDENTIFICATION\ncopy "en_US"\nEND LC_IDENTIFICATION\nLC_CTYPE\ncopy "en_US"\nEND LC_CTYPE\nLC_COLLATE\n'
}
three() { # name, order_start, weights of a b ch c A U+0301 -
    { hdr
      for s in L1-a L1-b L1-c L2-0 L2-1 L2-2 L3-0 L3-1; do echo "collating-symbol <$s>"; done
      echo 'collating-element <ch> from "<U0063><U0068>"'
      echo "order_start $2"
      for s in L1-a L1-b L1-c L2-0 L2-1 L2-2 L3-0 L3-1; do echo "<$s>"; done
      echo "<U0061> $3"; echo "<U0062> $4"; echo "<ch> $5"; echo "<U0063> $6"; echo "<U0041> $7"
      echo "<U0301> $8"; echo "<U002D> $9"
      printf 'UNDEFINED IGNORE;IGNORE;IGNORE\norder_end\nEND LC_COLLATE\n'; } > "$out/src/$1"
    localedef -c -i "$out/src/$1" -f UTF-8 "$out/$1.utf8" >/dev/null 2>&1 || true
}
five() { # name, order_start, weights of a b c d e
    { hdr
      for s in S1 S2 S3 S4; do echo "collating-symbol <$s>"; done
      echo "order_start $2"
      for s in S1 S2 S3 S4; do echo "<$s>"; done
      echo "<U0061> $3"; echo "<U0062> $4"; echo "<U0063> $5"; echo "<U0064> $6"; echo "<U0065> $7"
      printf 'UNDEFINED IGNORE;IGNORE;IGNORE;IGNORE\norder_end\nEND LC_COLLATE\n'; } > "$out/src/$1"
    localedef -c -i "$out/src/$1" -f UTF-8 "$out/$1.utf8" >/dev/null 2>&1 || true
}
w="<L1-a>;<L2-0> <L1-b>;<L2-0> <L1-c>;<L2-1> <L1-c>;<L2-0> <L1-a>;<L2-1> IGNORE;<L2-2> IGNORE;IGNORE"
three bp "forward;backward,position" $w
three pp "forward,position;position" $w
three b1 "backward;forward" $w
three l1 "forward" "<L1-a>" "<L1-b>" "<L1-c>" "<L1-c>" "<L1-a>" "IGNORE" "IGNORE"
five f2 "forward;forward" "<S1>;<S2>" "<S1>;IGNORE" "IGNORE;<S2>" "IGNORE;IGNORE" "IGNORE;<S3>"
five p2 "forward;forward,position" "<S1>;<S2>" "<S1>;IGNORE" "IGNORE;<S2>" "IGNORE;IGNORE" "IGNORE;<S3>"
five f3 "forward;forward;forward" "<S1>;<S2>;<S3>" "<S1>;IGNORE;IGNORE" "IGNORE;IGNORE;<S3>" \
    "IGNORE;IGNORE;IGNORE" "IGNORE;<S2>;IGNORE"
five p4 "forward;backward;forward;forward,position" "<S1>;<S2>;<S3>;<S4>" "<S1>;IGNORE;IGNORE;IGNORE" \
    "IGNORE;IGNORE;IGNORE;<S4>" "IGNORE;IGNORE;IGNORE;IGNORE" "IGNORE;<S2>;IGNORE;IGNORE"
{ hdr
  for s in S1 S2 S3 S4 S5 S6; do echo "collating-symbol <$s>"; done
  echo "script <BP>"
  echo "order_start forward;forward"
  for s in S1 S2 S3 S4 S5 S6; do echo "<$s>"; done
  echo "order_start <BP>;forward;backward,position"
  echo "<U0061> <S1>;<S4>"; echo "<U0062> <S2>;<S4>"; echo "<U0301> IGNORE;<S5>"; echo "<U002D> IGNORE;IGNORE"
  echo "order_start forward;forward"
  echo "<U0078> <S3>;<S6>"; echo "<U0079> <S3>;<S5>"
  printf 'UNDEFINED IGNORE;IGNORE\norder_end\nEND LC_COLLATE\n'; } > "$out/src/mx"
localedef -c -i "$out/src/mx" -f UTF-8 "$out/mx.utf8" >/dev/null 2>&1 || true
for l in bp pp b1 l1 f2 p2 f3 p4 mx; do
    [ -s "$out/$l.utf8/LC_COLLATE" ] || { echo "localedef: no $l" >&2; exit 1; }
done
