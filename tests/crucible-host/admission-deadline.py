#!/usr/bin/env python3
# Copyright (C) 2026 Greg Burd.
"""Catch renewed handshake budgets; real client, loopback TCP, no throughput claim."""
import os
import socket
import sys
import time
from faults import Client, Peer, u32

binary, mode = sys.argv[1:]
assert mode in ('normal', 'near', 'cohort', 'backpressure', 'cancel', 'eintr', 'progress')
# Backpressure fills the real socket below send(); the peer must not parse padding.
# EINTR/progress separately exercise retry paths, not a substitute for backpressure.
if mode in ('backpressure', 'cancel', 'eintr', 'progress'):
    os.environ['CRUCIBLE_ADMISSION_FAULT'] = 'backpressure' if mode == 'cancel' else mode
original_reply = Peer.reply

def reply(self, s, message):
    if message[:4] == u32(1):
        if mode in ('near', 'cohort'):
            time.sleep(1.4 if mode == 'near' else 2)
        elif mode != 'normal' and self.index == 0:
            if mode in ('backpressure', 'cancel'):
                s.setsockopt(socket.SOL_SOCKET, socket.SO_RCVBUF, 4096)
            time.sleep(4)
        original_reply(self, s, message)
        if mode in ('backpressure', 'cancel') and self.index == 0:
            while not self.stop:
                time.sleep(.01)
        return
    original_reply(self, s, message)

Peer.reply = reply
peers = [Peer(i) for i in range(3)]
c = Client(binary, peers)
try:
    before = time.monotonic()
    c.send('t' if mode == 'cancel' else 'c')
    response = c.result(11)
    elapsed = time.monotonic() - before
    print('mode=%s response=%r elapsed=%.3f cohort_budget=5' % (mode, response, elapsed), flush=True)
    if mode in ('backpressure', 'cancel'):
        pressure = c.cmd('j').split()
        print(' '.join(pressure), flush=True)
        assert pressure[0] == 'admission-pressure' and int(pressure[1]) > 0 and pressure[2] == '1', 'real TCP backpressure not reached'
    if mode in ('normal', 'near'):
        assert response == 'connected', 'valid admission refused'
        if mode == 'near':
            assert 4 <= elapsed < 5.8, 'near-budget schedule not reached'
        # Normal I/O must NOT inherit the now-expired admission deadline.
        time.sleep(max(0, before + 5.2 - time.monotonic()))
        assert c.cmd('w') == 'write 0'
        assert c.cmd('f') == 'flush 0'
        assert c.cmd('r') == 'read 0 66'
    elif mode == 'cancel':
        fields = response.split()
        assert fields[:2] == ['admission-cancel', '1']
        assert int(fields[2]) < 750, 'cancellation did not promptly join admission'
    else:
        assert response == 'error Socket operation deadline exceeded', response
        assert 4.7 <= elapsed < 6, 'admission send escaped shared five-second deadline'
    assert c.cmd('d') == 'disconnected'
    c.send('q'); assert c.p.wait(timeout=2) == 0
    assert not any(p.errors for p in peers), [p.errors for p in peers]
    print('PASS admission-deadline', mode, flush=True)
finally:
    c.close()
    for p in peers: p.close()
