#!/usr/bin/env python3
# Copyright (C) 2026 Greg Burd.
"""Run only against the explicitly supplied pinned downstairs executable."""
import os
from pathlib import Path
import socket
import subprocess
import sys
import time
from faults import Client

server, client, root = sys.argv[1:]
root = Path(root).resolve()
root.mkdir(exist_ok=False)
ports = [18810, 18820, 18830]
processes, logs = [], []

def start():
    for i, port in enumerate(ports):
        log = open(root / ('server%d.log' % i), 'ab')
        logs.append(log)
        processes.append(subprocess.Popen([server, 'run', '--address', '127.0.0.1',
            '--port', str(port), '--data', str(root / str(i))], stdout=log, stderr=log))
    for port in ports:
        deadline = time.monotonic() + 10
        while True:
            assert all(p.poll() is None for p in processes), 'server exited'
            try:
                with socket.create_connection(('127.0.0.1', port), timeout=.1): break
            except OSError:
                assert time.monotonic() < deadline, 'server startup timeout'
                time.sleep(.05)

def stop():
    for p in processes: p.terminate()
    for p in processes:
        try: p.wait(timeout=5)
        except subprocess.TimeoutExpired: p.kill(); p.wait()
    processes.clear()
    for log in logs: log.close()
    logs.clear()

def connect():
    return Client(client, [], ['127.0.0.1:'+str(p) for p in ports])

c = None
try:
    for i in range(3):
        subprocess.run([server, 'create', '--data', str(root / str(i)), '--uuid',
            '00000000-0000-0000-0000-000000000000', '--extent-size', '8',
            '--extent-count', '1'], check=True, stdout=sys.stdout, stderr=sys.stderr)
    start()
    c = connect()
    assert c.cmd('c') == 'connected'
    assert c.cmd('r') == 'read 0 0'
    assert c.cmd('w') == 'write 0'
    assert c.cmd('f') == 'flush 0'
    assert c.cmd('r') == 'read 0 66'
    # Wait for snapshot barrier (all three) without requiring filesystem snapshots:
    # normal flush returns 2/3; use a short settle only before controlled restart.
    time.sleep(.5)
    assert c.cmd('d') == 'disconnected'
    c.send('q'); assert c.p.wait(timeout=5) == 0
    c.close(); c = None
    stop(); start()
    c = connect()
    assert c.cmd('c').startswith('error'), 'persisted generation 1 was reused'
    assert c.cmd('g 2') == 'reopened'
    assert c.cmd('R') == 'read 0 66', 'real-server persisted read mismatch'
    c.send('q'); assert c.p.wait(timeout=5) == 0
    print('PASS real pinned server: clean read/write/flush/read, restart refusal, explicit generation 2 persisted read')
finally:
    if c: c.close()
    stop()
