#!/usr/bin/env python3
import subprocess
import argparse
import glob
import time
import sys
import os

import tests.test_net
import tests.test_tracing

from operator import attrgetter
from tests.testing import *

host_arch = os.uname().machine

os.environ["LC_ALL"]="C"
os.environ["LANG"]="C"

blacklist= [
    "tst-dns-resolver.so",
    "tst-feexcept.so",
]

qemu_blacklist= [
    "tcp_close_without_reading_on_fc"
]

firecracker_blacklist= [
    "tracing_smoke_test",
    "tcp_close_without_reading_on_qemu"
]

#At this point there are 100 out of 131 unit tests that pass on aarch64.
#The remaining ones are blacklisted below until we fix various
#issues that prevent those tests from passing.
aarch64_blacklist= [
    #All java tests require JVM running on aarch64 which in turn at least requires TLS support
    "java_isolated",
    "java_non_isolated",
    "java_no_wrapper",
    "java-perms",
    #Following tests crash with message 'Assertion failed: type == ARCH_JUMP_SLOT (core/elf.cc: relocate_pltgot: 789)'
    "tst-async.so",
    "tst-promise.so",
    "tst-sigaltstack.so",
    #Remaining tests below fail for various different reasons
    #Please see comments on the right side for more details
    "tst-condvar.so",              # To few cpus?
    "tst-dlfcn.so",                # Fails with page fault - 'relocate_rela(): unknown relocation type 0000000000000008'
    "tst-elf-permissions.so",      # Infinite page fault
    "tst-eventfd.so",              # Seems to hang after 'running simple threaded test'
    "tst-except.so",               # Crashes with 'failed looking up symbol _Unwind_Resume'
    "tst-fpu.so",                  # Seems to hang, possibly floating point related
    "tst-hub.so",                  # Seems to hang after 'Starting stress test'
    "tst-libc-locking.so",         # Hangs
    "tst-mmap-file.so",            # Some assertions fail - 'SUMMARY: 30 tests, 4 failures'
    "tst-mmap.so",                 # Infinite page fault
    "tst-pthread-barrier.so",      # Some assertions fail - 'SUMMARY: 8 tests / 1 failures', with cpu >= 2 seems to hang
    "tst-ring-spsc-wraparound.so", # Hangs
    "tst-sampler.so",              # Crashes with 'failed looking up symbol _ZN4prof13start_samplerENS_6configE (prof::start_sampler(prof::config))'
    "tst-semaphore.so",            # Seems to hang after 'Thread *: Incremented 1th' messages
    "tst-stdio-rofs.so",           # One assertion fails - 'tst-stdio.cc(1922): fatal error: in "STDIO_TEST_fread_unbuffered_pathological_performance": critical check (t1 - t0) <= (1) has failed'
    "tst-time.so",                 # One assertion fails - 'tst-time.cc(70): fatal error: in "time_time": critical check (static_cast<time_t>(0)) != (t1) has failed'
    "tst-timerfd.so",              # Some assertions fail - 'SUMMARY: 212 tests, 10 failures'
    "tst-yield.so",                # Seems to hang
    #These tests fail due to some other shortcomings in the test scripts
    "tracing_smoke_test",
    "tcp_close_without_reading_on_fc",
    "tcp_close_without_reading_on_qemu",
    #The tests below are NOT blacklisted but occasionally hang so I am listing
    #them here for completeness.
    #tst-queue-mpsc.so,            # Sometimes hangs
    #tst-tcp-nbwrite.so,           # Sometimes hangs
    #tst-concurrent-init.so,       # Sometimes hangs
    #tst-bsd-kthread.so,           # Once failed with page fault
]

add_tests([
    SingleCommandTest('java_isolated', '/java_isolated.so -cp /tests/java/tests.jar:/tests/java/isolates.jar \
        -Disolates.jar=/tests/java/isolates.jar org.junit.runner.JUnitCore io.osv.AllTestsThatTestIsolatedApp'),
    SingleCommandTest('java_non_isolated', '/java.so -cp /tests/java/tests.jar:/tests/java/isolates.jar \
        -Disolates.jar=/tests/java/isolates.jar org.junit.runner.JUnitCore io.osv.AllTestsThatTestNonIsolatedApp'),
    SingleCommandTest('java_no_wrapper', '/usr/bin/java -cp /tests/java/tests.jar \
        org.junit.runner.JUnitCore io.osv.BasicTests !'),
    SingleCommandTest('java-perms', '/java_isolated.so -cp /tests/java/tests.jar io.osv.TestDomainPermissions'),
])

class TestRunnerTest(SingleCommandTest):
    def __init__(self, name):
        super(TestRunnerTest, self).__init__(name, '/tests/%s' % name)

# Not all files in build/release/tests/tst-*.so may be on the test image
# (e.g., some may have actually remain there from old builds) - so lets take
# the list of tests actually in the image form the image's manifest file.
test_files = []
is_comment = re.compile("^[ \t]*(|#.*|\[manifest])$")
is_test = re.compile("^/tests/tst-.*.so")

def collect_tests():
    with open(cmdargs.manifest, 'r') as f:
        for line in f:
            line = line.rstrip();
            if is_comment.match(line): continue;
            components = line.split(": ", 2);
            guestpath = components[0].strip();
            hostpath = components[1].strip()
            if is_test.match(guestpath):
                test_files.append(guestpath);
    add_tests((TestRunnerTest(os.path.basename(x)) for x in test_files))

def run_test(test):
    sys.stdout.write("  TEST %-35s" % test.name)
    sys.stdout.flush()

    start = time.time()
    try:
        test.set_run_py_args(run_py_args)
        test.set_hypervisor(cmdargs.hypervisor)
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
    run([test for test in tests if not isinstance(test, TestRunnerTest)])

    blacklist_tests = ' '.join(blacklist)
    args = run_py_args + ["-s", "-e", "/testrunner.so -b %s" % (blacklist_tests)]
    if subprocess.call(["./scripts/run.py"] + args):
        exit(1)

def run(tests):
    for test in sorted(tests, key=attrgetter('name')):
        if is_not_skipped(test):
            run_test(test)
        else:
            sys.stdout.write("  TEST %-35s SKIPPED\n" % test.name)
            sys.stdout.flush()

def pluralize(word, count):
    if count == 1:
        return word
    return word + 's'

def run_tests():
    start = time.time()

    if cmdargs.name:
        tests_to_run = list((t for t in tests if re.match('^' + cmdargs.name + '$', t.name)))
        if not tests_to_run:
            print('No test matches: ' + cmdargs.name)
            exit(1)
    else:
        tests_to_run = tests

    if cmdargs.single:
        if tests_to_run != tests:
            print('Cannot restrict the set of tests when --single option is used')
            exit(1)
        run_tests_in_single_instance()
    else:
        run(tests_to_run)

    end = time.time()

    duration = end - start
    print(("OK (%d %s run, %.3f s)" % (len(tests_to_run), pluralize("test", len(tests_to_run)), duration)))

def main():
    collect_tests()
    while True:
        run_tests()
        if not cmdargs.repeat:
            break

if __name__ == "__main__":
    parser = argparse.ArgumentParser(prog='test')
    parser.add_argument("-v", "--verbose", action="store_true", help="verbose test output")
    parser.add_argument("-r", "--repeat", action="store_true", help="repeat until test fails")
    parser.add_argument("-s", "--single", action="store_true", help="run as much tests as possible in a single OSv instance")
    parser.add_argument("-p", "--hypervisor", action="store", default="qemu", help="choose hypervisor to run: qemu, firecracker")
    parser.add_argument("-n", "--name", action="store", help="run all tests whose names match given regular expression")
    parser.add_argument("--run_options", action="store", help="pass extra options to run.py")
    parser.add_argument("-m", "--manifest", action="store", default="modules/tests/usr.manifest", help="test manifest")
    parser.add_argument("-b", "--blacklist", action="append", help="test to be blacklisted", default=[])
    parser.add_argument("--arch", action="store", choices=["x86_64","aarch64"], default=host_arch,
                        help="specify QEMU architecture: x86_64, aarch64")
    cmdargs = parser.parse_args()
    set_verbose_output(cmdargs.verbose)
    if cmdargs.run_options != None:
        run_py_args = cmdargs.run_options.split()
    else:
        run_py_args = []
    if cmdargs.arch != None:
        run_py_args = run_py_args + ['--arch', cmdargs.arch]
    if cmdargs.hypervisor == 'firecracker':
        blacklist.extend(firecracker_blacklist)
    else:
        blacklist.extend(qemu_blacklist)
    if cmdargs.arch == 'aarch64':
        blacklist.extend(aarch64_blacklist)
    blacklist.extend(cmdargs.blacklist)
    main()
