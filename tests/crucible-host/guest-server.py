#!/usr/bin/env python3
# Copyright (C) 2026 Greg Burd.
"""Fresh loopback-only downstairs + QEMU user-network guest compatibility."""
from pathlib import Path
import socket
import subprocess
import sys
import time
server, kernel, root = map(Path, sys.argv[1:])
root.mkdir(exist_ok=False)
processes, logs = [], []
try:
    for i, port in enumerate([18910, 18920, 18930]):
        subprocess.run([str(server), 'create', '--data', str(root/str(i)), '--uuid',
            '00000000-0000-0000-0000-000000000000', '--extent-size', '8', '--extent-count', '1'], check=True)
        log = open(root/('server%d.log' % i), 'wb'); logs.append(log)
        processes.append(subprocess.Popen([str(server), 'run', '--address', '127.0.0.1',
            '--port', str(port), '--data', str(root/str(i))], stdout=log, stderr=log))
    time.sleep(.5)
    assert all(p.poll() is None for p in processes)
    # QEMU's user-network host alias forwards to host loopback; no external bind.
    result = subprocess.run(['sudo', '-n', sys.executable, 'scripts/run.py', '-k',
        '-c', '2', '-m', '512', '--vnc', 'none',
        '--crucible0=192.168.122.1:18910,192.168.122.1:18920,192.168.122.1:18930',
        '--crucible0-uuid=00000000-0000-0000-0000-000000000000', '--crucible0-generation=1',
        '--execute=--verbose --nomount /guest-smoke.so /dev/crucible0'], cwd=kernel,
        capture_output=True, text=True, timeout=25)
    print(result.stdout); print(result.stderr)
    assert result.returncode == 0
    assert 'SUCCESS: created device crucible0' in result.stdout
    assert 'PASS Crucible guest real device zero read' in result.stdout
    print('PASS guest boot generation -> actual block driver -> pinned servers')
finally:
    for p in processes: p.terminate()
    for p in processes:
        try: p.wait(timeout=5)
        except subprocess.TimeoutExpired: p.kill(); p.wait()
    for log in logs: log.close()
