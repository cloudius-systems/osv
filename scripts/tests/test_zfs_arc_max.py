# Copyright (C) 2026 Greg Burd
#
# This work is open source software, licensed under the terms of the
# BSD license as described in the LICENSE file in the top-level directory.

# --zfs-arc-max must bound the ZFS ARC.  One boot writes a 192 MiB file of
# incompressible data on the ZFS root.  Fresh boots then read it with the
# default ceiling (most of a 2 GB guest) and with --zfs-arc-max=64M in an
# 8 GB guest, where the ARC's default minimum (RAM/32, 256 MiB) is larger
# than the ceiling, and report the free memory the reads used: the default
# ARC keeps all of the file, the capped one at most 64 MiB.  The last boot
# deletes the file, so it does not stay in the shared test image.

from tests.testing import *
import os
import re

arch = os.uname().machine
def set_arch(_arch):
    global arch
    arch = _arch

def run_probe(mode, kernel_args='', memsize='2G'):
    run_args = ['-m', memsize]
    if os.uname().machine != arch:
        run_args += ['--arch', arch]
    guest = Guest(['-e', kernel_args + '/tests/misc-zfs-arc-probe.so ' + mode],
                  run_py_args=run_args)
    line = wait_for_line_starts(guest, 'arc-probe ')
    guest.join()
    return line

def probe_used(kernel_args='', memsize='2G'):
    line = run_probe('read', kernel_args, memsize)
    m = re.match(r'arc-probe size=(\d+) used=(-?\d+)$', line)
    assert m, line
    return int(m.group(1)), int(m.group(2))

@test
def zfs_arc_max():
    manifest = os.path.join(osv_base, 'build/last/usr.manifest')
    with open(manifest) as f:
        if '/tests/misc-zfs-arc-probe.so' not in f.read():
            print('SKIP (not a ZFS image)', end=' ')
            return
    assert run_probe('create') == 'arc-probe created'
    try:
        size, used_default = probe_used()
        _, used_capped = probe_used('--zfs-arc-max=64M ', '8G')
    finally:
        assert run_probe('delete') == 'arc-probe deleted'
    mb = 1 << 20
    print('file %d MB, ARC used %d MB default, %d MB capped in 8 GB' %
          (size // mb, used_default // mb, used_capped // mb), end=' ')
    assert used_default > size * 3 // 4, 'the default ARC did not cache the file'
    assert used_capped < used_default - (size - 64 * mb) // 2
