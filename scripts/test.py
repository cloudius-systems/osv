#!/usr/bin/env python
import subprocess
import argparse
import glob
import time
import sys
import os
import re

from operator import attrgetter


class Test(object):
    def __init__(self, name, command, handled_by_testrunner=False):
        self.name = name
        self.command = command
        self.handled_by_testrunner = handled_by_testrunner

class StandardOSvTest(Test):
    def __init__(self, name):
        super(StandardOSvTest, self).__init__(name, '/tests/%s' % name, True)

blacklist = [
    "tst-fsx.so" # Test fails
  , "tst-dns-resolver.so"
]

java_test = Test('java', '/java.so -cp /tests/java/tests.jar:/tests/java/isolates.jar \
    -Disolates.jar=/tests/java/isolates.jar org.junit.runner.JUnitCore io.osv.AllTests')

test_files = set(glob.glob('build/release/tests/tst-*.so')) - set(glob.glob('build/release/tests/*-stripped.so'))
standard_tests = [StandardOSvTest(os.path.basename(x)) for x in test_files]
tests = sorted([java_test] + standard_tests, key=attrgetter('name'))

def scan_errors(s):
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
        "cannot execute ",

        # Below are generic error patterns for error case.
        # A test should indicate it's status by a return value only:
        #   0              - on success
        #   non-zero value - on failure
        # The below messages are printed by the OSv and are promissed to be
        # supported in the future.
        "Assertion failed",
        "Aborted",
        "program exited with status",
        r"program tests/(.*?) returned",
        "Exception was caught while running",
        "at org.junit.runner.JUnitCore.main"
    ]
    for pattern in patterns:
        if re.findall(pattern, s):
            return True
    return False

def run_test(test):
    sys.stdout.write("  TEST %-25s" % test.name)
    sys.stdout.flush()

    start = time.time()
    args = ["-s", "-e", test.command]
    process = subprocess.Popen(["./scripts/run.py"] + args, stdout=subprocess.PIPE)
    out = ""
    line = ""
    while True:
        ch = process.stdout.read(1).decode()
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
        sys.stdout.write("Test %s FAILED\n" % test.name)
        sys.stdout.flush()
        exit(1)
    else:
        duration = end - start
        sys.stdout.write(" OK  (%.3f s)\n" % duration)
        sys.stdout.flush()

def is_not_skipped(test):
    return test.name not in blacklist

def run_tests_in_single_instance():
    run(filter(lambda test: not test.handled_by_testrunner, tests))

    blacklist_tests = ' '.join(blacklist)
    args = ["-s", "-e", "/testrunner.so -b %s" % (blacklist_tests)]
    if subprocess.call(["./scripts/run.py"] + args):
        exit(1)

def run(tests):
    for test in tests:
        if is_not_skipped(test):
            run_test(test)
        else:
            sys.stdout.write("  TEST %-25s SKIPPED\n" % test.name)
            sys.stdout.flush()

def run_tests():
    start = time.time()

    if cmdargs.single:
        run_tests_in_single_instance()
    else:
        run(tests)

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
    parser.add_argument("-s", "--single", action="store_true", help="run as much tests as possible in a single OSv instance")
    cmdargs = parser.parse_args()
    main()
