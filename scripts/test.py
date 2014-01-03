#!/usr/bin/env python2
import subprocess
import argparse
import glob
import time
import sys
import os
import re

blacklist = [
    "tst-fsx.so" # Test fails
]

tests = sorted([os.path.basename(x) for x in glob.glob('build/release/tests/tst-*.so')])

def scan_errors(s, name):
    if not s:
        return False
    patterns = [
        # These are a legacy error patterns printed outside the tests themselves
        # The test writer should not assume these patterns are going to
        # supported in the future and should indicate a test status as described
        # below.
        "failures detected in test",
        "failure detected in test",
        "FAIL",
        "cannot execute tests/%s" % name,

        # Below are generic error patterns for error case.
        # A test should indicate it's status by a return value only:
        #   0              - on success
        #   non-zero value - on failure
        # The below messages are printed by the OSv and are promissed to be
        # supported in the future.
        "Assertion failed",
        "Aborted",
        "program exited with status",
        "program tests/%s returned" % name
    ]
    for pattern in patterns:
        if re.findall(pattern, s):
            return True
    return False

def run_test(name):
    sys.stdout.write("  TEST %-25s" % name)
    sys.stdout.flush()

    start = time.time()
    args = ["-s", "-e", "tests/%s" % (name)]
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
            if not cmdargs.verbose and scan_errors(line, name):
                sys.stdout.write(out)
                sys.stdout.flush()
                cmdargs.verbose = True
            line = ""

    end = time.time()

    if scan_errors(out, name) or process.returncode:
        sys.stdout.write("Test %s FAILED\n" % name)
        sys.stdout.flush()
        exit(1)
    else:
        duration = end - start
        sys.stdout.write(" OK  (%.3f s)\n" % duration)
        sys.stdout.flush()

def run_tests_in_single_instance():
    blacklist_tests = ' '.join(blacklist)

    args = ["-s", "-e", "/testrunner.so -b %s" % (blacklist_tests)]
    subprocess.call(["./scripts/run.py"] + args)

def run_tests():
    start = time.time()

    if cmdargs.single:
        run_tests_in_single_instance()
    else:
        for test in tests:
            if not test in blacklist:
                run_test(test)
            else:
                sys.stdout.write("  TEST %-25s SKIPPED\n" % test)
                sys.stdout.flush()

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
    parser.add_argument("-s", "--single", action="store_true", help="run all tests in a single OSv instance")
    cmdargs = parser.parse_args()
    main()
