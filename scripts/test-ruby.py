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

disabled_list = [
]

class TestRunnerTest(SingleCommandTest):
    def __init__(self, name):
        super(TestRunnerTest, self).__init__(name, '--nohalt --cwd=/tests/%s /ruby.so /tests/%s' % (os.path.dirname(name), name))

test_files = set(glob.glob('apps/ruby/upstream/ruby/test/*/*.rb'))
test_files |= set(glob.glob('apps/ruby/upstream/ruby/test/*.rb'))
add_tests((TestRunnerTest(os.path.relpath(x, 'apps/ruby/upstream/ruby/test/')) for x in test_files))
failed_test = 0

def run_test(test):
    sys.stdout.write("  TEST %-25s" % test.name)
    sys.stdout.flush()

    start = time.time()
    try:
        test.run()
        end = time.time()

        duration = end - start
        sys.stdout.write(" OK  (%.3f s)\n" % duration)
        sys.stdout.flush()
    except:
        sys.stdout.write(" FAILED\n")
        sys.stdout.flush()
	global failed_test
	failed_test += 1

def is_not_skipped(test):
    return test.name not in disabled_list

def run_tests_in_single_instance():
    run(filter(lambda test: not isinstance(test, TestRunnerTest), tests))

    disabled_tests = ' '.join(disabled_list)
    args = ["-s", "-e", "--nohalt /ruby.so %s" % (disabled_tests)]
    if subprocess.call(["./scripts/run.py"] + args):
        exit(1)

def run(tests):
    for test in sorted(tests, key=attrgetter('name')):
        if is_not_skipped(test):
            run_test(test)
        else:
            sys.stdout.write("  TEST %-25s SKIPPED\n" % test.name)
            sys.stdout.flush()

def pluralize(word, count):
    if count == 1:
        return word
    return word + 's'

def run_tests():
    start = time.time()

    if cmdargs.test:
        test = next((t for t in tests if t.name == cmdargs.test), None)
        if not test:
            print 'No such test: ' + cmdargs.test
            exit(1)
        tests_to_run = [test]
    else:
        tests_to_run = tests

    if cmdargs.single:
        if tests_to_run != tests:
            print 'Cannot restrict the set of tests when --single option is used'
            exit(1)
        run_tests_in_single_instance()
    else:
        run(tests_to_run)

    end = time.time()

    duration = end - start
    global failed_test
    print("(%d %s run, %d failed, %.3f s)" % (len(tests_to_run), pluralize("test", len(tests_to_run)), failed_test, duration))

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
    parser.add_argument("--test", action="store", help="run a single test")
    cmdargs = parser.parse_args()
    set_verbose_output(cmdargs.verbose)
    main()
