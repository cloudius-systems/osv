#!/usr/bin/env python3
# Copyright (C) 2026 Greg Burd
# BSD license, see LICENSE in the top-level directory.
"""Run production NUMA discovery under host ASan/UBSan (not an OSv boot).

Requires C++17, Boost headers, and the pinned external/x64/acpica submodule.
Only AcpiGetTable, scheduler CPUs and debug output are replaced. No copied
parser or ACPICA layouts. Every fixture is a separately allocated table;
this tests parser-local safety, not ACPICA firmware ingestion/checksumming.
"""
import os
from pathlib import Path
import subprocess
import tempfile

root = Path(__file__).resolve().parents[1]
cases = ['valid', 'short-cpu', 'short-x2apic', 'short-memory', 'short-slit',
         'domain-bound', 'max-pxm', 'bad-tail', 'overflow-memory', 'empty-memory',
         'boundaries', 'sparse', 'ignored', 'slit-bounds']
(root / 'build').mkdir(exist_ok=True)
with tempfile.TemporaryDirectory(prefix='numa-parser-', dir=root / 'build') as tmp:
    tmp = Path(tmp)
    (tmp / 'osv').mkdir()
    (tmp / 'osv/sched.hh').write_text('''#pragma once
#include <vector>
namespace sched {
struct cpu { unsigned id; struct { unsigned apic_id; } arch; };
inline cpu cpu0{0, {7}}, cpu1{1, {9}};
inline std::vector<cpu*> cpus{&cpu0, &cpu1};
}
''')
    (tmp / 'osv/debug.hh').write_text('#pragma once\ninline void debugf(const char*, ...) {}\n')
    (tmp / 'osv/drivers_config.h').write_text('#define CONF_drivers_acpi 1\n')
    exe = tmp / 'numa-parser'
    subprocess.run([os.environ.get('CXX', 'g++'), '-std=c++17', '-O1', '-g',
                    '-Wall', '-Wextra', '-Werror', '-fsanitize=address,undefined',
                    '-fno-sanitize-recover=all', '-fno-omit-frame-pointer', '-no-pie',
                    '-I' + str(tmp), '-I' + str(root / 'include'),
                    '-isystem', str(root / 'external/x64/acpica/source/include'),
                    str(root / 'tests/tst-numa-parser.cc'), '-o', str(exe)], check=True)
    failures = []
    for case in cases:
        result = subprocess.run([str(exe), case], timeout=15)
        if result.returncode:
            failures.append(case)
    print(f'{len(cases) - len(failures)}/{len(cases)} cases passed; failures: {failures}', flush=True)
    raise SystemExit(bool(failures))
