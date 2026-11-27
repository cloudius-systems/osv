#!/usr/bin/env python3
# Copyright (C) 2026 Greg Burd. BSD license, see LICENSE.
"""Host sanitizer checks of exact production syscall bodies and core/numa.cc.

linux.cc cannot be linked on Linux (scheduler/OSv syscall dispatcher). Extract
its complete NUMA policy block and getcpu block without rewriting either body;
replace only the scheduler and logging. Private immutable topology is populated
with compact fixtures; firmware ingestion has its own tst-numa-parser.py suite.
"""
import argparse
import hashlib
import os
import re
from pathlib import Path
import subprocess
import tempfile

parser = argparse.ArgumentParser(description=__doc__)
parser.add_argument('--baseline', action='store_true',
                    help='pre-query base has no node_of_phys API; skip only that new API')
parser.add_argument('--native-boundary-log', type=Path,
                    help='compare OSv boundary results to the pinned Debian reviewer receipt')
args = parser.parse_args()
root = Path(__file__).resolve().parents[1]
source = (root / 'linux.cc').read_text()
policy = source[source.index('#define MPOL_DEFAULT'):source.index('#if CONF_syscall_sys_sched_getaffinity')]
cpu = source[source.index('#if CONF_syscall_sys_getcpu'):source.index('#if CONF_syscall_sys_set_robust_list')]
cases = ['default', 'allowed', 'bounds', 'flags', 'reset', 'high-padding',
         'placement', 'cpu', 'unaligned']
if not args.baseline:
    cases.append('physical')
(root / 'build').mkdir(exist_ok=True)
with tempfile.TemporaryDirectory(prefix='numa-query-', dir=root / 'build') as tmp:
    tmp = Path(tmp)
    (tmp / 'osv').mkdir()
    (tmp / 'osv/sched.hh').write_text('''#pragma once
namespace sched {
struct cpu { unsigned id; static cpu* current(); };
inline cpu current_cpu{0};
inline cpu* cpu::current() { return &current_cpu; }
}
''')
    (tmp / 'osv/debug.hh').write_text('#pragma once\ninline void debugf(const char*, ...) {}\n')
    (tmp / 'osv/drivers_config.h').write_text('#define CONF_drivers_acpi 0\n')
    (tmp / 'query-bodies.hh').write_text(policy + '\n' + cpu)
    exe = tmp / 'numa-queries'
    subprocess.run([os.environ.get('CXX', 'g++'), '-std=c++17', '-O1', '-g',
                    '-Wall', '-Wextra', '-Werror', '-Wno-unused-parameter',
                    '-fsanitize=address,undefined', '-fno-sanitize-recover=all',
                    '-fno-omit-frame-pointer', '-no-pie',
                    '-DCONF_syscall_get_mempolicy=1', '-DCONF_syscall_set_mempolicy=1',
                    '-DCONF_syscall_sys_getcpu=1',
                    '-DHAVE_NODE_OF_PHYS=' + str(int('physical' in cases)),
                    '-I' + str(tmp), '-I' + str(root / 'include'),
                    str(root / 'tests/tst-numa-queries.cc'), '-o', str(exe)], check=True)
    failures = []
    for case in cases:
        result = subprocess.run([str(exe), case], timeout=15,
                                stdout=subprocess.PIPE, text=True)
        print(result.stdout, end='', flush=True)
        if case == 'high-padding' and args.native_boundary_log and not result.returncode:
            receipt = args.native_boundary_log.read_bytes()
            # Actual reviewer receipt: Debian6.12.111 x64, NODES_SHIFT10,
            # Seccomp0. This comparator documents a divergence, not equality
            # with every Linux build or an OSv bitmap capacity of1024.
            assert hashlib.sha256(receipt).hexdigest() == (
                'af889712860d3078bf33881d6c5f43da4ebb3284e8eae0529bb301db00320650')
            native = {(int(n), int(b)): (int(rc), int(err)) for n, b, rc, err in
                      re.findall(r'maxnode=(\d+).*?word16bit=(-?\d+).*?rc=(-?\d+) errno=(\d+)',
                                 receipt.decode())}
            actual = {(int(n), int(b)): (int(rc), int(err)) for n, b, rc, err in
                      re.findall(r'boundary maxnode=(\d+) word16bit=(-?\d+) rc=(-?\d+) errno=(\d+)',
                                 result.stdout)}
            assert len(native) == len(actual) == 16 and native.keys() == actual.keys()
            differences = {key for key in actual if actual[key] != native[key]}
            assert differences == {(1026, 1), (1026, 63)}, differences
            for key in sorted(differences):
                assert native[key] == (-1, 22) and actual[key] == (0, 0)
                print(f'documented divergence {key}: Linux={native[key]} OSv={actual[key]}')
            print('16 native receipt comparisons: 14 equal, 2 deliberate divergences')
        if result.returncode:
            failures.append(case)
    print(f'{len(cases)-len(failures)}/{len(cases)} passed; failures: {failures}', flush=True)
    raise SystemExit(bool(failures))
