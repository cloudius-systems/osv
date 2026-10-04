#!/usr/bin/env python3
# Copyright (C) 2026 Greg Burd.
"""Exercise the actual runner parser and emitted QEMU append arguments."""
from pathlib import Path
import subprocess
import sys
root = Path(__file__).resolve().parents[2]
for option in ['--crucible-generation=2', '--crucible0-generation=3', '--crucible7-generation=18446744073709551615']:
    command = [sys.executable, str(root / 'scripts/run.py'), '-k', '--dry-run',
               option, '-e', '/guest-smoke.so']
    result = subprocess.run(command, cwd=root, text=True, capture_output=True, check=True)
    assert option in result.stdout and '-append' in result.stdout, result.stdout
print('PASS runner generation forwarding')
