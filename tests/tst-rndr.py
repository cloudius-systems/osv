#!/usr/bin/env python3
# Copyright (C) 2026 Greg Burd
#
# This work is open source software, licensed under the terms of the
# BSD license as described in the LICENSE file in the top-level directory.
"""Run the kernel's RNDR read with controlled architectural RNDR outcomes.

The RNDR code in drivers/random.cc and live_entropy_sources_feed() are
extracted and compiled into a host program, with only the MRS replaced by a
load of the flags each step should report. The CSET, the retry loop and the
entropy feeder are the production code. This tests failure handling, not the
hardware's entropy quality. It is not run by scripts/build check.

On an aarch64 Linux host with a C++ compiler:
    CXX=g++ RUNNER= python3 tests/tst-rndr.py

On an x86 host, with a cross compiler that has no C library of its own (as
Fedora's aarch64-linux-gnu-g++), use OSv's downloaded aarch64 sysroot (from
scripts/download_aarch64_packages.py) and qemu-aarch64-static:
    S=$PWD/build/downloaded_packages/aarch64/gcc/install
    CXX="aarch64-linux-gnu-g++ --sysroot=$S -isystem $S/usr/include/c++/<ver> \\
        -isystem $S/usr/include/c++/<ver>/aarch64-redhat-linux -B $S/usr/lib64 -L $S/usr/lib64" \\
    RUNNER="qemu-aarch64-static -L $S" python3 tests/tst-rndr.py
"""
import os
from pathlib import Path
import shlex
import subprocess
import tempfile

root = Path(__file__).resolve().parents[1]
def replace_once(text, old, new):
    if text.count(old) != 1:
        raise RuntimeError('expected one extraction match: ' + repr(old))
    return text.replace(old, new)

src = (root / 'drivers/random.cc').read_text()
arm = src[src.index('static constexpr int rndr_retries_max'):src.index('\n#endif', src.index('static constexpr int rndr_retries_max'))]
# Inject NZCV=0000 (success) or 0100 (failure) per the Arm RNDR specification.
arm = replace_once(arm, '"mrs %0, s3_3_c2_c4_0\\n\\t"',
                  '"ldp %0, %1, [%2]\\n\\t" "msr nzcv, %1\\n\\t"')
arm = replace_once(arm, '                     :\n', '                     : "r"(next_draw())\n')
# The injected LDP reads memory, unlike the production MRS.
arm = replace_once(arm, ': "cc");', ': "cc", "memory");')
feed = (root / 'bsd/sys/dev/random/live_entropy_sources.cc').read_text()
feed = feed[feed.index('void\nlive_entropy_sources_feed('):feed.index('\nbool\nlive_entropy_sources_empty(')]
fixture = (root / 'tests/tst-rndr.cc.inc').read_text()
with tempfile.TemporaryDirectory(prefix='tst-rndr-') as d:
    source = Path(d) / 'test.cc'
    source.write_text(replace_once(fixture, '// KERNEL_CODE', arm + '\n' + feed))
    exe = Path(d) / 'test'
    subprocess.run(shlex.split(os.environ.get('CXX', 'aarch64-linux-gnu-g++')) +
                   ['-std=gnu++17', '-O2', '-Wall', '-Wextra', '-Werror',
                    '-Wno-unused-parameter', str(source), '-o', str(exe)], check=True, timeout=120)
    subprocess.run(shlex.split(os.environ.get('RUNNER', 'qemu-aarch64')) + [str(exe)], check=True, timeout=30)
