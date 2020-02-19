#!/usr/bin/env python3
import atexit
import subprocess
import argparse
import tempfile
import glob
import time
import sys
import os

import tests.test_net
import tests.test_tracing

from operator import attrgetter
from tests.testing import *

os.environ["LC_ALL"]="C"
os.environ["LANG"]="C"

blacklist= [
    "tst-dns-resolver.so",
    "tst-feexcept.so",
]

firecracker_blacklist= [
    "tracing_smoke_test",
    "tcp_close_without_reading", #This test is flaky under firecracker due to a bug possibly in firecracker itself
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
with open('modules/tests/usr.manifest', 'r') as f:
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

def make_export_and_conf():
    export_dir = tempfile.mkdtemp(prefix='share')
    os.chmod(export_dir, 0o777)
    (conf_fd, conf_path) = tempfile.mkstemp(prefix='export')
    conf = os.fdopen(conf_fd, "w")
    conf.write("%s 127.0.0.1(insecure,rw)\n" % export_dir)
    conf.flush()
    conf.close()
    return (conf_path, export_dir)

proc = None

def kill_unfsd():
    global proc
    subprocess.call(["sudo", "kill", str(proc.pid + 1)])
    proc.wait()

UNFSD = "./modules/nfs-tests/unfsd.bin"

def run_tests():
    global proc
    start = time.time()

    if cmdargs.nfs:
        pass
    elif cmdargs.name:
        tests_to_run = list((t for t in tests if re.match('^' + cmdargs.name + '$', t.name)))
        if not tests_to_run:
            print('No test matches: ' + cmdargs.name)
            exit(1)
    else:
        tests_to_run = tests

    if cmdargs.nfs:
        if not os.path.exists(UNFSD):
            print("Please do:\n\tmake nfs-server")
            sys.exit(1)
        (conf_path, export_dir) = make_export_and_conf()
        proc = subprocess.Popen([ "sudo",
                                 os.path.join(os.getcwd(), UNFSD),
                                 "-t",
                                 "-d",
                                 "-s",
                                 "-l", "127.0.0.1",
                                 "-e", conf_path ],
                                 stdin = sys.stdin,
                                 stdout = subprocess.PIPE,
                                 stderr = sys.stderr,
                                 shell = False)
        atexit.register(kill_unfsd)
        tests_to_run = [ SingleCommandTest('nfs-test',
            "/tst-nfs.so --server 192.168.122.1 --share %s" %
            export_dir) ]

        line = proc.stdout.readline().decode()
        while line:
             print(line)
             if "/tmp" in line:
                break
             line = proc.stdout.readline().decode()
             

        run(tests_to_run)
        kill_unfsd()
    elif cmdargs.single:
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
    while True:
        run_tests()
        if not cmdargs.repeat:
            break

if __name__ == "__main__":
    parser = argparse.ArgumentParser(prog='test')
    parser.add_argument("-v", "--verbose", action="store_true", help="verbose test output")
    parser.add_argument("-r", "--repeat", action="store_true", help="repeat until test fails")
    parser.add_argument("-s", "--single", action="store_true", help="run as much tests as possible in a single OSv instance")
    parser.add_argument("-n", "--nfs",    action="store_true", help="run nfs test in a single OSv instance")
    parser.add_argument("-p", "--hypervisor", action="store", default="qemu", help="choose hypervisor to run: qemu, firecracker")
    parser.add_argument("--name", action="store", help="run all tests whose names match given regular expression")
    parser.add_argument("--run_options", action="store", help="pass extra options to run.py")
    cmdargs = parser.parse_args()
    set_verbose_output(cmdargs.verbose)
    if cmdargs.run_options != None:
        run_py_args = cmdargs.run_options.split()
    else:
        run_py_args = []
    if cmdargs.hypervisor == 'firecracker':
        blacklist.extend(firecracker_blacklist)
    main()
