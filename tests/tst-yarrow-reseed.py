#!/usr/bin/env python3
# Copyright (C) 2026 Greg Burd
# BSD license; see LICENSE in the top-level directory.
"""Host regression of actual Yarrow/crypto and software adaptor function bodies.

OS mutex/sleep/harvest boundaries are supplied by the fixture. No entropy
hardware is registered. Fork copies the full process state (including local
static caches); it is NOT an OSv hypervisor snapshot/resume integration test.
Run: python3 tests/tst-yarrow-reseed.py [unseeded|tail|serialization]
CC/CXX and TEST_CFLAGS may select sanitizers. No third-party test dependency.
"""
import os
from pathlib import Path
import re
import shlex
import subprocess
import sys
import tempfile

root = Path(__file__).resolve().parents[1]
base = root / 'bsd/sys'

def function(src, name):
    marker = '\n' + name + '('
    if src.count(marker) != 1:
        raise RuntimeError('expected one function definition: ' + name)
    start = src.index(marker)
    begin = src.index('{', start)
    depth = 1
    end = begin + 1
    while depth:
        depth += (src[end] == '{') - (src[end] == '}')
        end += 1
    return src[start:end]

def without_includes(text):
    return re.sub(r'^#include .*$', '', text, flags=re.M)

with tempfile.TemporaryDirectory(prefix='tst-yarrow-') as d:
    d = Path(d)
    # Only crypto headers: don't shadow host sys headers with FreeBSD's.
    for rel in ['crypto/rijndael/rijndael.h', 'crypto/rijndael/rijndael_local.h',
                'crypto/rijndael/rijndael-api-fst.h', 'crypto/sha2/sha2.h', 'dev/random/hash.h']:
        dest = d / rel
        dest.parent.mkdir(parents=True, exist_ok=True)
        dest.write_text((base / rel).read_text())
    common = '''#include <sys/types.h>
#include <sys/param.h>
#include <stdint.h>
#include <stddef.h>
#include <string.h>
#include <assert.h>
#define __FBSDID(x)
#define OSV_LIBC_API
#define OSV_LIBSOLARIS_API
#include <crypto/rijndael/rijndael.h>
#include <crypto/rijndael/rijndael_local.h>
#include <crypto/rijndael/rijndael-api-fst.h>
#include <crypto/sha2/sha2.h>
#include <dev/random/hash.h>
'''
    objects = []
    flags = shlex.split(os.environ.get('TEST_CFLAGS', ''))
    for rel in ['crypto/rijndael/rijndael-alg-fst.c', 'crypto/rijndael/rijndael-api-fst.c',
                'crypto/sha2/sha2.c', 'dev/random/hash.c']:
        src = d / Path(rel).name
        src.write_text(common + without_includes((base / rel).read_text()))
        obj = src.with_suffix('.o')
        subprocess.run(shlex.split(os.environ.get('CC', 'cc')) + ['-O1', '-g', '-Wall', '-Wextra',
                       '-I' + str(d), '-c', str(src), '-o', str(obj)] + flags, check=True, timeout=120)
        objects.append(str(obj))
    yarrow = without_includes((base / 'dev/random/yarrow.cc').read_text())
    soft = (base / 'dev/random/randomdev_soft.cc').read_text()
    bodies = ('void' + function(soft, 'randomdev_unblock') + '\nstatic int' +
              function(soft, 'randomdev_block') + '\nstatic void' +
              function(soft, 'randomdev_flush_reseed'))
    fixture = (root / 'tests/tst-yarrow-reseed.cc.inc').read_text()
    headers = '\n'.join(without_includes((base / rel).read_text()) for rel in
                        ['sys/random.h', 'dev/random/randomdev_soft.h'])
    if fixture.count('// KERNEL_HEADERS') != 1 or fixture.count('// KERNEL_CODE') != 1:
        raise RuntimeError('fixture insertion markers changed')
    fixture = fixture.replace('// KERNEL_HEADERS', headers)
    source = d / 'test.cc'
    source.write_text(common + fixture.replace('// KERNEL_CODE', yarrow + bodies))
    exe = d / 'test'
    subprocess.run(shlex.split(os.environ.get('CXX', 'c++')) + ['-std=gnu++17', '-O1', '-g',
                   '-Wall', '-Wextra', '-Werror', '-pthread', '-I' + str(d), str(source)] +
                   objects + flags + ['-o', str(exe)], check=True, timeout=120)
    cases = sys.argv[1:] or ['unseeded', 'tail', 'serialization']
    for case in cases:
        subprocess.run([str(exe), case], check=True, timeout=30)
