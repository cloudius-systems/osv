#!/usr/bin/env python3
# Copyright (C) 2026 Greg Burd
# BSD license; see LICENSE in the top-level directory.
"""Run exact vring code with host boundaries; not a guest/DMA/scheduler test."""
import os
from pathlib import Path
import re
import subprocess
import tempfile

HERE = Path(__file__).resolve().parent
ROOT = HERE.parent.parent


def without_includes(path):
    return re.sub(r'^#include.*$', '', path.read_text(), flags=re.M)


parts = [(HERE / 'boundary.hh').read_text(),
         without_includes(ROOT / 'drivers/virtio-vring.hh'),
         without_includes(ROOT / 'drivers/virtio-vring.cc'),
         (HERE / 'malformed.cc.inc').read_text()]
with tempfile.TemporaryDirectory(prefix='osv-malformed-') as tmp:
    cc, exe = Path(tmp) / 'test.cc', Path(tmp) / 'test'
    cc.write_text('\n'.join(parts))
    subprocess.run([os.environ.get('CXX', 'g++'), '-std=gnu++17', '-O1', '-g',
                    '-Wall', '-Wextra',
                    *os.environ.get('SANITIZE', '-fsanitize=address,undefined -fno-omit-frame-pointer').split(),
                    str(cc), '-o', str(exe)], check=True)
    failed = False
    cases = [(mode, kind) for kind in ('direct', 'indirect')
             for mode in ('valid', 'id', 'id-max', 'gc-id', 'gc-id-size')]
    cases += [(mode, 'direct') for mode in
              ('next', 'next-size', 'self-cycle', 'cycle', 'full-cycle')]
    for mode, kind in cases:
        result = subprocess.run([str(exe), mode, kind], timeout=10)
        expected = 0 if mode == 'valid' else 86
        ok = result.returncode == expected
        print(f'{mode}/{kind}: {"PASS" if ok else "FAIL"} '
              f'(exit {result.returncode}, expected {expected})', flush=True)
        failed |= not ok
    raise SystemExit(int(failed))
