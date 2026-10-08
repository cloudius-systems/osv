#!/usr/bin/env python3
# Copyright (C) 2026 Greg Burd
# BSD license; see LICENSE in the top-level directory.
"""Run actual RNG constructor/callbacks and transport registration on host shims.

No hardware/scheduler emulation claim: guest tests cover real completions.
Run on x64: python3 tests/tst-virtio-rng-irq.py [mmio|pci|msix]
Both ISA callback branches and PCI-off MMIO are compiled. ARM here means
preprocessor coverage, not ARM execution. CXX/TEST_CFLAGS select sanitizers.
"""
import os
from pathlib import Path
import re
import shlex
import subprocess
import sys
import tempfile

root = Path(__file__).resolve().parents[1]


def definition(path, signature):
    text = (root / path).read_text()
    assert text.count(signature) == 1, signature
    start = text.index(signature)
    begin = text.index('\n{', start) + 1
    end, depth = begin + 1, 1
    while depth:
        depth += (text[end] == '{') - (text[end] == '}')
        end += 1
    return text[start:end]


def header(path):
    return re.sub(r'^#include .*$', '', (root / path).read_text(), flags=re.M)


# Preserve the production configuration include: flags must not materialize
# macros a real translation unit never includes (an earlier fixture hid this).
config_include = '\n'.join(line for line in (root / 'drivers/virtio-rng.cc').read_text().splitlines()
                           if line == '#include <osv/drivers_config.h>')
rng = config_include + '\n' + '\n'.join(definition('drivers/virtio-rng.cc', s) for s in [
    'rng::rng(', 'rng::~rng()', 'size_t rng::get_random_bytes(',
    'void rng::handle_irq()', 'bool rng::ack_irq()', 'hw_driver* rng::probe('])
transport = '\n'.join(definition(path, sig) for path, sig in [
    ('drivers/virtio-mmio.cc', 'void mmio_device::register_interrupt('),
    ('drivers/virtio-mmio.cc', 'u8 mmio_device::read_and_ack_isr()'),
    ('drivers/virtio-pci-device.cc', 'void virtio_pci_device::register_interrupt(')])
factory = (root / 'drivers/virtio-device.hh').read_text()
factory = factory[factory.index('struct interrupt_factory {'):]
factory = factory[:factory.index('\n};') + 3]
fixture = (root / 'tests/tst-virtio-rng-irq.cc.inc').read_text()
fixture = fixture.replace('// FACTORY', factory).replace('// RNG_HEADER', header('drivers/virtio-rng.hh'))
fixture = fixture.replace('// ACTUAL_CODE', 'namespace virtio {\n' + transport + '\n' + rng + '\n}')
cases = sys.argv[1:] or ['mmio', 'pci', 'msix']
failed = False
with tempfile.TemporaryDirectory(prefix='rng-irq-') as tmp:
    source = Path(tmp) / 'test.cc'
    source.write_text(fixture)
    for arm, pci in [(0, 1), (1, 1), (0, 0), (1, 0)]:
        exe = Path(tmp) / f'test-{arm}-{pci}'
        flags = ['-D__aarch64__'] if arm else []
        config = Path(tmp) / 'osv/drivers_config.h'
        config.parent.mkdir(exist_ok=True)
        config.write_text(f'#define CONF_drivers_pci {pci}\n#define CONF_drivers_mmio 1\n')
        subprocess.run(shlex.split(os.environ.get('CXX', 'c++')) + [
            '-std=gnu++17', '-O1', '-g', '-Wall', '-Wextra', '-Werror',
            '-I' + tmp,
            str(source), '-o', str(exe)] + flags +
            shlex.split(os.environ.get('TEST_CFLAGS', '')), check=True, timeout=120)
        for case in cases:
            if pci or case == 'mmio':
                print(f'CASE ARM={arm} PCI={pci} {case}', flush=True)
                result = subprocess.run([str(exe), case], timeout=10)
                failed |= result.returncode != 0
sys.exit(1 if failed else 0)
