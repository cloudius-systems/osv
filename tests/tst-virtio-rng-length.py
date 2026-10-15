#!/usr/bin/env python3
# Copyright (C) 2026 Greg Burd
# BSD license; see LICENSE in the top-level directory.
"""Actual refill body; host queue/lock/abort boundaries, not guest DMA testing.

Run on Debian HOST: python3 tests/tst-virtio-rng-length.py [--old|--mutants]
Every compilation uses NDEBUG and ASan+UBSan. Checks do not use C assert.
--old requires ASan heap-buffer-overflow from the unchanged actual body.
"""
import argparse
import os
from pathlib import Path
import shlex
import subprocess
import tempfile

p = argparse.ArgumentParser()
p.add_argument('--old', action='store_true')
p.add_argument('--mutants', action='store_true')
a = p.parse_args()
root = Path(__file__).resolve().parents[1]


def check(condition, message):
    if not condition:
        raise RuntimeError(message)


def definition(path, signature):
    text = (root / path).read_text()
    check(text.count(signature) == 1, 'unique production signature required')
    start = text.index(signature)
    begin = text.index('{', start)
    end, depth = begin + 1, 1
    while depth:
        depth += (text[end] == '{') - (text[end] == '}')
        end += 1
    return text[start:end]


body = definition('drivers/virtio-rng.cc', 'void rng::refill()')
get_elem = definition('drivers/virtio-vring.cc', 'vring::get_buf_elem(u32* len)')
fixture = (root / 'tests/tst-virtio-rng-length.cc.inc').read_text()
check(fixture.count('// ACTUAL_REFILL') == 1, 'unique refill placeholder required')
check(fixture.count('// ACTUAL_GET_BUF_ELEM') == 1, 'unique queue placeholder required')
fixture = fixture.replace('// ACTUAL_GET_BUF_ELEM', 'void* ' + get_elem)

with tempfile.TemporaryDirectory(prefix='rng-length-') as tmp:
    tmp = Path(tmp)
    def compile_body(code, label):
        src = tmp / (label + '.cc')
        exe = tmp / label
        src.write_text(fixture.replace('// ACTUAL_REFILL', code))
        subprocess.run(shlex.split(os.environ.get('CXX', 'c++')) + [
            '-std=gnu++17', '-O1', '-g', '-DNDEBUG', '-Wall', '-Wextra', '-Werror',
            '-fsanitize=address,undefined', '-fno-omit-frame-pointer',
            str(src), '-o', str(exe)], check=True, timeout=120)
        return exe

    def run(exe, cap, length, drain, negative=False):
        result = subprocess.run([str(exe), str(cap), str(length), str(drain)],
                                text=True, stdout=subprocess.PIPE, stderr=subprocess.STDOUT,
                                timeout=15)
        print(f'CASE {exe.name} cap={cap} len={length} drain={drain} exit={result.returncode}', flush=True)
        print(result.stdout, flush=True)
        if negative:
            check(result.returncode != 0, 'mutant unexpectedly passed')
            check(negative in result.stdout,
                  'negative must fail its intended case, not timeout/compiler/startup error')
        else:
            check(result.returncode == 0 and 'PASS capacity=' in result.stdout,
                  'actual refill case failed')
        return result

    exe = compile_body(body, 'actual')
    if a.old:
        for cap in [64, 16, 1]:
            run(exe, cap, cap + 1, 0, negative='AddressSanitizer: heap-buffer-overflow')
        print('PASS: unchanged actual refill has intended ASan OOB for valid submitted buffers')
    else:
        for cap in [64, 16, 1]:
            for drain in [0, 1]:
                for length in sorted({0, 1, cap - 1, cap, cap + 1, 4294967295}):
                    run(exe, cap, length, drain)
        if a.mutants:
            guard = '''        if (len > remaining) {
            abort("virtio-rng: used length %u exceeds submitted capacity %zu", len, remaining);
        }
'''
            check(body.count(guard) == 1, 'expected exact production guard for mutation')
            mutations = [
                ('remove', body.replace(guard, ''), 16, 17, 0),
                ('constant64', body.replace('len > remaining', 'len > _pool_size'), 16, 17, 0),
                ('reject-equal', body.replace('len > remaining', 'len >= remaining'), 16, 16, 0),
                ('wrong-direction', body.replace('len > remaining', 'len < remaining'), 16, 1, 0),
                ('current-space', body.replace('len > remaining', 'len > _pool_size - _entropy.size()'), 16, 17, 1),
                ('wrong-reason', body.replace('virtio-rng: used length', 'wrong-device: used length'), 16, 17, 0),
            ]
            for label, code, cap, length, drain in mutations:
                mutant = compile_body(code, label)  # compilation error is never a caught negative
                expected = ('CHECK failed: f.reason == wanted' if label == 'wrong-reason'
                            else 'CHECK failed: length > capacity' if label in ['reject-equal', 'wrong-direction']
                            else 'AddressSanitizer: heap-buffer-overflow')
                run(mutant, cap, length, drain, negative=expected)
            print('PASS: all six compiled mutants rejected by actual runtime gate')
