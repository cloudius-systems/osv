#!/usr/bin/env python2
import subprocess
import argparse
import glob
import time
import sys
import os
import re

blacklist = [
    "tst-bsd-callout.so",
    "tst-ctxsw.so",
    "tst-leak.so",
    "tst-lfring.so",
    "tst-mmap-anon-perf.so",
    "tst-mutex.so",
    "tst-panic.so",
    "tst-random.so",
    "tst-sockets.so",
    "tst-tcp-hash-srv.so",
    "tst-wake.so",
    "tst-zfs-disk.so",
]

tests = sorted([os.path.basename(x) for x in glob.glob('build/release/tests/tst-*.so')])

def scan_errors(s):
    if not s:
        return False
    patterns = [
        "failures detected in test",
        "failure detected in test",
        "Assertion failed",
        "FAIL"
    ]
    for pattern in patterns:
        if re.findall(pattern, s):
            return True
    return False

def run_test(name):
    sys.stdout.write("  TEST %-25s" % name)
    sys.stdout.flush()

    start = time.time()
    args = ["-g", "-e", "tests/%s" % (name)]
    process = subprocess.Popen(["./scripts/run.py"] + args, stdout=subprocess.PIPE)
    out = ""
    line = ""
    while True:
        ch = process.stdout.read(1)
        if ch == '' and process.poll() != None:
            break
        out += ch
        if ch != '' and cmdargs.verbose:
            sys.stdout.write(ch)
            sys.stdout.flush()
        line += ch
        if ch == '\n':
            if not cmdargs.verbose and scan_errors(line):
                sys.stdout.write(out)
                sys.stdout.flush()
                cmdargs.verbose = True
            line = ""

    end = time.time()

    if scan_errors(out) or process.returncode:
        sys.stdout.write("Test %s FAILED\n" % name)
        sys.stdout.flush()
        exit(1)
    else:
        duration = end - start
        sys.stdout.write(" OK  (%.3f s)\n" % duration)
        sys.stdout.flush()

def run_tests():
    start = time.time()

    for test in tests:
        if not test in blacklist:
            run_test(test)

    end = time.time()

    duration = end - start
    print("OK (%d tests run, %.3f s)" % (len(tests), duration))

def main():
    while True:
        run_tests()
        if not cmdargs.repeat:
            break

if (__name__ == "__main__"):
    parser = argparse.ArgumentParser(prog='test')
    parser.add_argument("-v", "--verbose", action="store_true", help="verbose test output")
    parser.add_argument("-r", "--repeat", action="store_true", help="repeat until test fails")
    cmdargs = parser.parse_args()
    main()
