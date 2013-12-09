#!/usr/bin/env python2
import subprocess
import sys
import re

tests = [
    "/tests/tst-except.so",
    "/tests/tst-mmap.so",
    "/tests/tst-mmap-file.so",
    "/tests/tst-rename.so",
    "/tests/tst-vfs.so",
    "/tests/tst-stat.so",
    "/tests/tst-utimes.so",
    "/tests/tst-libc-locking.so",
    "/tests/tst-strerror_r.so",
    "/tests/tst-pipe.so",
    "/tests/tst-fs-link.so",
    "/tests/tst-threadcomplete.so",
    "/tests/tst-huge.so",
]

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
    print("Running '%s'..." % name)
    args = ["-g", "-e", name]
    process = subprocess.Popen(["./scripts/run.py"] + args, stdout=subprocess.PIPE)
    out = ""
    while True:
        ch = process.stdout.read(1)
        if ch == '' and process.poll() != None:
            break
        out += ch
        if ch != '':
            sys.stdout.write(ch)
            sys.stdout.flush()

    if scan_errors(out) or process.returncode:
        print("Test '%s' FAILED" % name)
        exit(1)

def run_tests():
    for test in tests:
        run_test(test)

    print("OK (%d tests run)" % (len(tests)))

def main():
    run_tests()

if (__name__ == "__main__"):
    main()
