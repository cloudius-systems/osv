import re
import os
import sys
import signal
import subprocess
import threading
import socket

tests = []
_verbose_output = False

osv_base = os.path.join(os.path.dirname(os.path.abspath(__file__)), '../..')

class TestFailed(Exception):
    pass

class Test(object):
    def __init__(self, name):
        self.name = name
        self.run_py_args = []
        self.hypervisor = 'qemu'

    def set_run_py_args(self, args):
        self.run_py_args = args

    def set_hypervisor(self, hypervisor):
        self.hypervisor = hypervisor

    def run(self):
        pass

class SingleCommandTest(Test):
    def __init__(self, name, command):
        super(SingleCommandTest, self).__init__(name)
        self.command = command

    def run(self):
        run_command_in_guest(self.command, hypervisor=self.hypervisor, run_py_args=self.run_py_args).join()

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

def scan_errors(s,scan_for_failed_to_load_object_error=True):
    if not s:
        return False
    patterns = [
        # These are a legacy error patterns printed outside the tests themselves
        # The test writer should not assume these patterns are going to
        # supported in the future and should indicate a test status as described
        # below.
        "failure.*detected.*in.*test",
        "FAIL",
        "cannot execute ",

        # Below are generic error patterns for error case.
        # A test should indicate it's status by a return value only:
        #   0              - on success
        #   non-zero value - on failure
        # The below messages are printed by the OSv and are promised to be
        # supported in the future.
        "Assertion failed",
        "Aborted",
	    "Error",
	    "\[BUG\]",
	    "Failed looking up symbol",
	    "Failure",
        "program exited with status",
        r"program tests/(.*?) returned",
        "Exception was caught while running",
        "at org.junit.runner.JUnitCore.main",
        "ContextFailedException",
        "AppThreadTerminatedWithUncaughtException",
	    "\[backtrace\]"
    ]

    if scan_for_failed_to_load_object_error:
        patterns = patterns + ["Failed to load object"]

    for pattern in patterns:
        if re.findall(pattern, s):
            return True
    return False

class SupervisedProcess:
    def __init__(self, args, show_output=False, show_output_on_error=True, scan_for_failed_to_load_object_error=True, pipe_stdin=False):
        if pipe_stdin:
            self.process = subprocess.Popen(args, stdout=subprocess.PIPE, stdin=subprocess.PIPE)
        else:
            self.process = subprocess.Popen(args, stdout=subprocess.PIPE)
        self.pipe_stdin = pipe_stdin
        self.cv = threading.Condition()
        self.lines = []
        self.output_collector_done = False
        self.has_errors = False
        self.show_output = show_output
        self.show_output_on_error = show_output_on_error

        self.output_collector_thread = threading.Thread(target=self._output_collector)
        self.output_collector_thread.start()
        self.scan_for_failed_to_load_object_error = scan_for_failed_to_load_object_error
        self.line_with_err = ""

    def _output_collector(self):
        def append_line(line):
            self.cv.acquire()

            if not self.has_errors and scan_errors(line,self.scan_for_failed_to_load_object_error):
                self.has_errors = True
                self.line_with_err = line
                if self.show_output_on_error and not self.show_output:
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
        ch_bytes = bytes()
        while True:
            ch_bytes = ch_bytes + self.process.stdout.read(1)
            try:
                ch = ch_bytes.decode('utf-8')
                if ch == '' and self.process.poll() != None:
                    break
                line += ch
                if ch == '\n':
                    append_line(line)
                    line = ''
                ch_bytes = bytes()
            except UnicodeError:
                continue

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
        if self.pipe_stdin:
            self.process.stdin.close()
        if self.process.returncode:
            raise Exception('Guest failed (returncode=%d)' % self.process.returncode)
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

    def write_line_to_input(self, line):
        self.process.stdin.write(line + "\n")
        self.process.stdin.flush()

    def line_with_error(self):
        return self.line_with_err

def run_command_in_guest(command, **kwargs):
    common_parameters = ["-e", "--power-off-on-abort " + command]

    if kwargs.get('hypervisor') == 'firecracker':
        parameters = ["-l", "-m 2048M", "-n", "-c 4"] + common_parameters
    else:
        parameters = ["-s"] + common_parameters

    return Guest(parameters, **kwargs)

class Guest(SupervisedProcess):
    def __init__(self, args, forward=[], hold_with_poweroff=False, show_output_on_error=True,
                 scan_for_failed_to_load_object_error=True, run_py_args=[], hypervisor='qemu', pipe_stdin=False):

        if hypervisor == 'firecracker':
            run_script = os.path.join(osv_base, "scripts/firecracker.py")
            self.monitor_socket = None
            physical_nic = os.getenv('OSV_FC_NIC')
            if physical_nic:
               args.extend(['-p', physical_nic])
        else:
            run_script = os.path.join(osv_base, "scripts/run.py")

            if hold_with_poweroff:
                args.append('-H')

            self.monitor_socket = 'qemu-monitor'
            args.extend(['--pass-args=-monitor unix:%s,server,nowait' % self.monitor_socket])

            for rule in forward:
                args.extend(['--forward', 'tcp::%s-:%s' % rule])

            args.extend(['--block-device-cache', 'unsafe'])

        if _verbose_output:
            print('Running OSv on %s with parameters: [%s]' % (hypervisor, " ".join(args)))

        SupervisedProcess.__init__(self, [run_script] + run_py_args + args,
            show_output=_verbose_output,
            show_output_on_error=show_output_on_error,
            scan_for_failed_to_load_object_error=scan_for_failed_to_load_object_error,
            pipe_stdin=pipe_stdin)

    def kill(self):
        if self.monitor_socket != None:
            s = socket.socket(socket.AF_UNIX, socket.SOCK_STREAM)
            s.connect(self.monitor_socket)
            s.send('quit\n'.encode())
            self.join()
            s.close()
        else:
            os.kill(self.process.pid, signal.SIGINT)
            self.join()

def wait_for_line(guest, text):
    return _wait_for_line(guest, lambda line: line == text, text)

def wait_for_line_starts(guest, text):
    return _wait_for_line(guest, lambda line: line.startswith(text), text)

def wait_for_line_contains(guest, text):
    return _wait_for_line(guest, lambda line: text in line, text)

def _wait_for_line(guest, predicate, text):
    for line in guest.read_lines():
        if predicate(line):
            return
    raise Exception('Text not found in output: ' + text)

def add_tests(tests_to_add):
    tests.extend(tests_to_add)

def set_verbose_output(verbose_output):
    global _verbose_output
    _verbose_output = verbose_output
