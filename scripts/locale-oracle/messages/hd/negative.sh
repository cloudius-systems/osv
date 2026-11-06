#!/bin/bash
# Copyright (C) 2026 Greg Burd. This work is open source software, licensed under the terms of the
# BSD license as described in the LICENSE file in the top-level directory.
# hd/negative.sh <out dir>: each mutant (hd/mutants.sh) built into its own directory and run
# through hd/diff.sh, which must fail. Exit 0 only if every mutant built and was caught.
H=$(cd "$(dirname "$0")" && pwd); O=${1:-/tmp/hd-neg}; mkdir -p $O; bad=0
for m in plural cstop libc unknown; do
  B=$O/build-$m; mkdir -p $B
  if ! OUT=$B sh $H/build.sh $m > $O/build-$m.log 2>&1 || [ ! -x $B/gtp-osv ]; then
    echo "MUTANT $m BUILD-FAILED (see build-$m.log)"; bad=1; continue; fi
  BIN=$B $H/diff.sh $O/run-$m > $O/run-$m.log 2>&1; rc=$?
  echo "MUTANT $m diff rc=$rc $(grep RESULT $O/run-$m.log) $(grep ^CATALOG $O/run-$m.log | sed 's/.*unexpected/unexpected/') libc-langs-differ=$(grep -c 'differ=[1-9]' $O/run-$m/libc-msgs.txt)"
  [ $rc != 0 ] || bad=1
done
echo "NEGATIVE $([ $bad = 0 ] && echo PASS || echo FAIL)"; exit $bad
