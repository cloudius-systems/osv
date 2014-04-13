#!/usr/bin/env python
import subprocess
import argparse
import glob
import time
import sys
import os
import re

from operator import attrgetter
from tests.testing import *

blacklist = [
    "tst-fsx.so" # Test fails
  , "tst-dns-resolver.so"
]

add_tests([
    SingleCommandTest('java', '/java.so -cp /tests/java/tests.jar:/tests/java/isolates.jar \
        -Disolates.jar=/tests/java/isolates.jar org.junit.runner.JUnitCore io.osv.AllTests'),
])

class TestRunnerTest(SingleCommandTest):
    def __init__(self, name):
        super(TestRunnerTest, self).__init__(name, '/tests/%s' % name)

test_files = set(glob.glob('build/release/tests/tst-*.so')) - set(glob.glob('build/release/tests/*-stripped.so'))
add_tests((TestRunnerTest(os.path.basename(x)) for x in test_files))

def run_test(test):
    sys.stdout.write("  TEST %-25s" % test.name)
    sys.stdout.flush()

    start = time.time()
    try:
        test.run()
    except:
        sys.stdout.write("Test %s FAILED\n" % test.name)
        sys.stdout.flush()
        raise
    end = time.time()

    duration = end - start
    sys.stdout.write(" OK  (%.3f s)\n" % duration)
    sys.stdout.flush()

def is_not_skipped(test):
    return test.name not in blacklist

def run_tests_in_single_instance():
    run(filter(lambda test: not isinstance(test, TestRunnerTest), tests))

    blacklist_tests = ' '.join(blacklist)
    args = ["-s", "-e", "/testrunner.so -b %s" % (blacklist_tests)]
    if subprocess.call(["./scripts/run.py"] + args):
        exit(1)

def run(tests):
    for test in sorted(tests, key=attrgetter('name')):
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
    set_verbose_output(cmdargs.verbose)
    main()
