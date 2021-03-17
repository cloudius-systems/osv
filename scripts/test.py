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

disabled_list= [
    "tst-dns-resolver.so",
    "tst-feexcept.so", # On AArch64 the tests around floating point exceptions (SIGFPE) fail even on KVM
]

qemu_disabled_list= [
    "tcp_close_without_reading_on_fc"
]

firecracker_disabled_list= [
    "tracing_smoke_test",
    "tcp_close_without_reading_on_qemu"
]

#At this point there are 117 out of 131 unit tests that pass on aarch64.
#The remaining ones are disabled below until we fix various
#issues that prevent those tests from passing.
aarch64_disabled_list= [
    #All java tests require JVM running on aarch64 which in turn at least requires TLS support
    "java_isolated",
    "java_non_isolated",
    "java_no_wrapper",
    "java-perms",
    #Following tests crash with message 'Assertion failed: type == ARCH_JUMP_SLOT (core/elf.cc: relocate_pltgot: 789)'
    "tst-sigaltstack.so",
    #Remaining tests below fail for various different reasons
    #Please see comments on the right side for more details
    "tst-condvar.so",              # To few cpus?
    "tst-elf-permissions.so",      # Infinite page fault
    "tst-mmap.so",                 # Infinite page fault
    "tst-pthread-barrier.so",      # Some assertions fail - 'SUMMARY: 8 tests / 1 failures', with cpu >= 2 seems to hang
    "tst-stdio-rofs.so",           # One assertion fails - 'tst-stdio.cc(1922): fatal error: in "STDIO_TEST_fread_unbuffered_pathological_performance": critical check (t1 - t0) <= (1) has failed'
    "tst-time.so",                 # One assertion fails - 'tst-time.cc(70): fatal error: in "time_time": critical check (static_cast<time_t>(0)) != (t1) has failed'
    "tst-timerfd.so",              # Some assertions fail - 'SUMMARY: 212 tests, 10 failures'
    #These tests fail due to some other shortcomings in the test scripts
    "tracing_smoke_test",
    "tcp_close_without_reading_on_fc",
    "tcp_close_without_reading_on_qemu",
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

def running_with_kvm_on(arch, hypervisor):
    if os.path.exists('/dev/kvm') and arch == host_arch and hypervisor in ['qemu', 'qemu_microvm', 'firecracker']:
        return True
    return False

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
    return test.name not in disabled_list

def run_tests_in_single_instance():
    run([test for test in tests if not isinstance(test, TestRunnerTest)])

    disabled_tests = ' '.join(disabled_list)
    args = run_py_args + ["-s", "-e", "/testrunner.so -d %s" % (disabled_tests)]
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
    parser.add_argument("-d", "--disabled_list", action="append", help="test to be disabled", default=[])
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
        disabled_list.extend(firecracker_disabled_list)
    else:
        disabled_list.extend(qemu_disabled_list)

    if cmdargs.arch == 'aarch64':
        disabled_list.extend(aarch64_disabled_list)
        #The SMP (#vCPUs >= 2) support on AArch64 is still pretty flaky (see issue #1123), so let us force to single cpu_list)
        run_py_args = run_py_args + ['-c', '1']

    if running_with_kvm_on(cmdargs.arch, cmdargs.hypervisor) and cmdargs.arch != 'aarch64':
        disabled_list.remove("tst-feexcept.so")

    disabled_list.extend(cmdargs.disabled_list)
    main()
