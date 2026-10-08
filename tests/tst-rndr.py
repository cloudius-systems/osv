#!/usr/bin/env python3
# Copyright (C) 2026 Greg Burd
# BSD license; see LICENSE in the top-level directory.
"""Run the kernel RNDR paths with controlled architectural RNDR outcomes.

Usage: CXX=aarch64-linux-gnu-g++ python3 tests/tst-rndr.py
Requires qemu-aarch64 (or RUNNER='' on Arm). Only the MRS is replaced;
the compiled AArch64 CSET, retry/read loops and entropy feeder are production
code. This tests failure handling, not the hardware's entropy quality.
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
read = src[src.index('static int\nrandom_read('):src.index('static int\nrandom_write(')]
feed = (root / 'bsd/sys/dev/random/live_entropy_sources.cc').read_text()
feed = feed[feed.index('void\nlive_entropy_sources_feed('):feed.index('\nbool\nlive_entropy_sources_empty(')]
fixture = (root / 'tests/tst-rndr.cc.inc').read_text()
with tempfile.TemporaryDirectory(prefix='tst-rndr-') as d:
    source = Path(d) / 'test.cc'
    source.write_text(replace_once(fixture, '// KERNEL_CODE', arm + '\n' + read + '\n' + feed))
    exe = Path(d) / 'test'
    subprocess.run(shlex.split(os.environ.get('CXX', 'aarch64-linux-gnu-g++')) +
                   ['-std=gnu++17', '-O2', '-Wall', '-Wextra', '-Werror',
                    '-Wno-unused-parameter', str(source), '-o', str(exe)], check=True, timeout=120)
    subprocess.run(shlex.split(os.environ.get('RUNNER', 'qemu-aarch64')) + [str(exe)], check=True, timeout=30)
