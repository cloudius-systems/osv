import re
import os
import sys
import subprocess
import threading
import traceback

tests = []
_verbose_output = False

class TestFailed(Exception):
    pass

class Test(object):
    def __init__(self, name):
        self.name = name

    def run(self):
        pass

class SingleCommandTest(Test):
    def __init__(self, name, command):
        super(SingleCommandTest, self).__init__(name)
        self.command = command

    def run(self):
        run_guest_sync(self.command)

class test(Test):
    """
    Use as annotation on functions.

    """

    def __init__(self, f):
        super(test, self).__init__(f.__name__)
        self.f = f
        tests.append(self)

    def run(self):
        self.f()

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

class SupervisedProcess:
    def __init__(self, args, show_output=False):
        self.process = subprocess.Popen(args, stdout=subprocess.PIPE)
        self.cv = threading.Condition()
        self.lines = []
        self.output_collector_done = False
        self.has_errors = False
        self.show_output = show_output

        self.output_collector_thread = threading.Thread(target=self._output_collector)
        self.output_collector_thread.start()

    def _output_collector(self):
        def append_line(line):
            self.cv.acquire()

            if not self.has_errors and scan_errors(line):
                self.has_errors = True
                if not self.show_output:
                    sys.stdout.write(self.output)
                    sys.stdout.flush()
                    self.show_output = True

            if self.show_output:
                sys.stdout.write(line)
                sys.stdout.flush()

            self.lines.append(line)
            self.cv.notify()
            self.cv.release()

        line = ''
        while True:
            ch = self.process.stdout.read(1).decode()
            if ch == '' and self.process.poll() != None:
                break
            line += ch
            if ch == '\n':
                append_line(line)
                line = ''

        if line:
            append_line(line)

        self.cv.acquire()
        self.output_collector_done = True
        self.cv.notify()
        self.cv.release()

    def read_lines(self):
        next_line = 0

        self.cv.acquire()

        while True:
            while len(self.lines) > next_line:
                line = self.lines[next_line]
                next_line += 1
                self.cv.release()
                yield line.rstrip('\n\r')
                self.cv.acquire()

            if self.output_collector_done:
                break

            self.cv.wait()

        self.cv.release()

    def join(self):
        self.output_collector_thread.join()
        if self.process.returncode:
            raise Exception('Guest failed (returncode=%d)' % self.proces.returncode)
        if self.failed:
            raise Exception('Guest failed')

    @property
    def output(self):
        self.cv.acquire()
        out = ''.join(self.lines)
        self.cv.release()
        return out

    def is_alive(self):
        return self.process.poll() == None

    @property
    def failed(self):
        assert not self.output_collector_thread.isAlive()
        return self.has_errors or self.process.returncode


def run_guest(command, forward=[]):
    osv_base = '.'
    run_script = os.path.join(osv_base, "scripts/run.py")
    args = ["-s", "-e", command]

    for rule in forward:
        args.extend(['--forward', 'tcp:%s::%s' % rule])

    return SupervisedProcess([run_script] + args, show_output=_verbose_output)

def run_guest_sync(*args, **kwargs):
    guest = run_guest(*args, **kwargs)
    guest.join()
    return guest

def wait_for_line(guest, text):
    for line in guest.read_lines():
        if line == text:
            return
    raise Exception('Text not found in output: ' + text)

def add_tests(tests_to_add):
    tests.extend(tests_to_add)

def set_verbose_output(verbose_output):
    global _verbose_output
    _verbose_output = verbose_output
