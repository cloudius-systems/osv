#!/bin/sh
# Copyright (C) 2026 Greg Burd. This work is open source software, licensed under the terms of the
# BSD license as described in the LICENSE file in the top-level directory.
# fuzz-mutants.sh <osv tree> <out dir> <seed files...>: the fuzz harness's negative test on the
# real code. Each mutant removes one check from a copy of libc/locale/mo.cc; the harness, built
# on it, must fail (ASan, UBSan or its own BUG abort, rc != 0) within 200000 iterations.
#   strbound  entry(): a string's end is not checked against the file size
#   tabbound  osv_mo_open(): the original table is not checked against the file size
#   depth     the plural parser's 9999-entry stack limit is not enforced (stack array overflow)
#   nplurals  osv_plural_index() returns the value even when it is not below nplurals
T=$1; O=$2; shift 2; mkdir -p $O; bad=0
for m in strbound tabbound depth nplurals; do
  cp $T/libc/locale/mo.cc $O/mo-$m.cc; cp $T/libc/locale/mo.h $O/mo.h
  case $m in
  strbound) sed -i 's/    if (uint64_t(o) + l >= m->size || m->base\[uint64_t(o) + l\]) {/    if (0) {/' $O/mo-$m.cc ;;
  tabbound) sed -i 's/    if (uint64_t(m->otab) + 8 \* uint64_t(m->n) > size ||/    if (0 \&\&/' $O/mo-$m.cc ;;
  depth)    sed -i 's/        if (1 + sp + 1 > max_stack) {/        if (0) {/; s/    size_t cap = len + 2 < max_stack ? len + 2 : max_stack;/    size_t cap = 16;/' $O/mo-$m.cc ;;
  nplurals) sed -i 's/    return r < p->nplurals ? r : 0;/    return r;/' $O/mo-$m.cc ;;
  esac
  if cmp -s $T/libc/locale/mo.cc $O/mo-$m.cc; then echo "MUTANT $m NOT-APPLIED"; bad=1; continue; fi
  g++ -std=gnu++17 -g -O1 -fsanitize=address,undefined -fno-sanitize-recover=all -I$O -I$T/libc/locale \
      $T/scripts/locale-oracle/messages/fuzz-mo.cc $O/mo-$m.cc -o $O/fuzz-$m || { echo "MUTANT $m BUILD-FAILED"; bad=1; continue; }
  timeout 600 $O/fuzz-$m 200000 7 "$@" > $O/fuzz-$m.log 2>&1; rc=$?
  echo "MUTANT $m rc=$rc $(grep -m1 -o 'ERROR: AddressSanitizer: [a-z-]*\|runtime error: [^:]*\|BUG: [a-z ]*\|DETERMINISTIC: [0-9]* failures' $O/fuzz-$m.log)"
  [ $rc != 0 ] || bad=1
done
echo "FUZZ-NEGATIVE $([ $bad = 0 ] && echo PASS || echo FAIL)"; exit $bad
