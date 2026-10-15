#!/usr/bin/env python3
# Copyright (C) 2026 Greg Burd
# BSD license; see LICENSE in the top-level directory.
"""Bounded host checks of production methods, not OSv scheduler/DMA validation."""
import os
from pathlib import Path
import re
import subprocess
import tempfile

HERE = Path(__file__).resolve().parent
ROOT = HERE.parent.parent


def without_includes(path):
    return re.sub(r'^#include.*$', '', path.read_text(), flags=re.M)


def method(text, signature):
    start = text.index(signature)
    opening = text.index('{', start)
    depth = 1
    end = opening + 1
    while depth:
        depth += (text[end] == '{') - (text[end] == '}')
        end += 1
    return text[start:end]


source = (ROOT / 'drivers/virtio-blk.cc').read_text()
parts = [
    (HERE / 'boundary.hh').read_text(),
    without_includes(ROOT / 'drivers/virtio-vring.hh'),
    (HERE / 'driver.hh').read_text(),
    without_includes(ROOT / 'drivers/virtio-vring.cc'),
    (HERE / 'full-ring.cc.inc').read_text(),
]
for signature in ['int blk::drain_queue(', 'bool blk::any_queue_not_empty(',
                  'void blk::req_done()', 'int blk::make_request(']:
    parts.append('namespace virtio {\n' + method(source, signature) + '\n}')
with tempfile.TemporaryDirectory(prefix='osv-vring-') as tmp:
    cc = Path(tmp) / 'test.cc'
    exe = Path(tmp) / 'test'
    cc.write_text('\n'.join(parts))
    subprocess.run([os.environ.get('CXX', 'g++'), '-std=gnu++17', '-O1', '-g',
                    '-Wall', '-Wextra', '-pthread',
                    *os.environ.get('SANITIZE', '').split(), str(cc), '-o', str(exe)],
                   check=True)
    subprocess.run([str(exe)], check=True, timeout=15)
