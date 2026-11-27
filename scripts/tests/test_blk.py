# Copyright (C) 2026 Greg Burd
# BSD license; see LICENSE in the top-level directory.
from tests.testing import *
import os
import tempfile

arch = os.uname().machine
def set_arch(_arch):
    global arch
    arch = _arch

# Run tst-blk-all-devices on a second NVMe controller, so that every per-CPU
# IO queue of the NVMe driver gets a request.  The root disk is the default
# virtio-blk one, vblk0, so the NVMe disk is vblk1.
@test
def blk_all_devices_nvme():
    global arch
    run_args = []
    if os.uname().machine != arch:
        run_args = ['--arch', arch]
    with tempfile.NamedTemporaryFile(prefix='osv-blk-', suffix='.img') as disk:
        disk.truncate(64 << 20)
        guest = Guest(['--second-nvme-image', disk.name,
                       '-e', '/tests/tst-blk-all-devices.so /dev/vblk1'],
                      run_py_args=run_args)
        wait_for_line(guest, 'PASS')
        guest.join()
