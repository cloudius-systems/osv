#!/usr/bin/env python3
# Copyright (C) 2026 Greg Burd
# SPDX-License-Identifier: BSD-2-Clause
"""Run the real scripts/build interface; a failed module command must not pass.

Use a disposable working directory and command doubles instead of building or
cleaning any real tree. The shell wrapper and its ERR trap are NOT substituted.
"""
import os
from pathlib import Path
import subprocess
import tempfile

wrapper = Path(__file__).resolve().parents[1] / 'scripts/build'
with tempfile.TemporaryDirectory(prefix='osv-build-status-') as directory:
    cwd = Path(directory)
    (cwd / 'bin').mkdir()
    (cwd / 'scripts').mkdir()
    make = cwd / 'bin/make'
    make.write_text('#!/bin/sh\nexit 0\n')
    make.chmod(0o755)
    module = cwd / 'scripts/module.py'
    module.write_text('#!/bin/sh\necho module-command-ran\nexit "$TEST_MODULE_STATUS"\n')
    module.chmod(0o755)
    env = dict(os.environ, PATH=str(cwd / 'bin') + ':' + os.environ['PATH'])
    env.pop('MAKEFLAGS', None)
    for expected in (0, 37):
        env['TEST_MODULE_STATUS'] = str(expected)
        result = subprocess.run(['bash', str(wrapper), 'clean'], cwd=cwd,
                                env=env, capture_output=True, text=True, timeout=10)
        assert 'module-command-ran' in result.stdout, result.stdout + result.stderr
        print(f'module status {expected}: wrapper status {result.returncode}', flush=True)
        assert result.returncode == expected, result.stdout + result.stderr
print('PASS: real build wrapper preserves success and failure status')
