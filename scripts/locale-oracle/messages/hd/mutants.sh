#!/bin/sh
# Copyright (C) 2026 Greg Burd. This work is open source software, licensed under the terms of the
# BSD license as described in the LICENSE file in the top-level directory.
# hd/mutants.sh <name> <src dir>: break one rule in the copied sources (hd/build.sh <name>), so the
# differential can be shown to fail on known-bad code.
#   plural    the default plural rule is n > 1 instead of n != 1
#   cstop     a LANGUAGE entry "C" does not end the list
#   libc      libc's messages are not translated
#   unknown   "Unknown error " is translated without its trailing space
set -e
m=$1; d=$2
case $m in
plural)  sed -i 's/r = n != 1;/r = n > 1;/' $d/mo.cc; grep -q 'r = n > 1;' $d/mo.cc ;;
cstop)   sed -i 's/                if (c_like(entry)) {/                if (0) {/' $d/gettext.cc; grep -q 'if (0) {' $d/gettext.cc ;;
libc)    sed -i 's/return __osv_dcngettext_l("libc", msg, nullptr, 1, LC_MESSAGES, l);/return msg;/' $d/messages.cc; grep -q '^    return msg;$' $d/messages.cc ;;
unknown) sed -i 's/tr("Unknown error ", l)/tr("Unknown error", l)/' $d/messages.cc; grep -q 'tr("Unknown error", l)' $d/messages.cc ;;
*) echo "unknown mutant $m"; exit 1 ;;
esac
