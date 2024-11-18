#!/usr/bin/python3
from testing import *
import argparse
import subprocess
import re
from time import sleep

def check_with_curl(url, expected_http_line):
    output = subprocess.check_output(["curl", "-s", url]).decode('utf-8')
    print(output)
    if expected_http_line not in output:
       print("\033[91mFAILED curl: wrong output\033[00m")
    print("------------")

def write_to_status_file(line):
    status_file_name = os.getenv('STATUS_FILE')
    if status_file_name:
       with open(status_file_name, "a+") as status_file:
         status_file.write(line + '\n')

def run(command, hypervisor_name, host_port, guest_port, http_path, expected_http_line=None,
        image_path=None, line=None, concurrency=50, count=1000, duration=10, threads=4, pre_script=None,
        no_keep_alive=False, error_line_to_ignore_on_kill = "", kernel_path=None, test_client='ab', qemu_tap='qemu_tap0'):

    py_args = []

    if kernel_path != None:
       print('Using kernel at %s' % kernel_path)
       if hypervisor_name == 'firecracker':
          py_args += ['-k', kernel_path]
       else:
          py_args += ['-k', '--kernel-path', kernel_path]

    if image_path != None:
        py_args = ['--image', image_path]

    app_url = None
    if hypervisor_name == 'qemu' and find_qemu_tap(qemu_tap):
        app = run_command_in_guest_on_qemu_with_tap(command, py_args, qemu_tap)
        app_url = "http://%s:%s%s" % (os.environ['OSV_HOSTNAME'], guest_port, http_path)
    else:
        app = run_command_in_guest(command, hypervisor=hypervisor_name, run_py_args=py_args, forward=[(host_port, guest_port)])

    if line != None:
        wait_for_line_contains(app, line)

    sleep(0.05)

    if pre_script != None:
        print(pre_script)
        subprocess.check_output([pre_script])

    if app_url == None:
        if hypervisor_name == 'firecracker':
            app_url = "http://172.16.0.2:%s%s" % (guest_port, http_path)
        else:
            app_url = "http://127.0.0.1:%s%s" % (host_port, http_path)

    if expected_http_line != None:
        check_with_curl(app_url, expected_http_line)

    if test_client == 'wrk':
        output = subprocess.check_output(["wrk", "--latency", "-t%d" % threads, "-c", "%d" % concurrency,
                                          "-d%ds" % duration, app_url]).decode('utf-8').split('\n')
        for line in output:
            print(line)
    else:
        ab_parameters = ["-l", "-c", str(concurrency), "-n", str(count), app_url]
        if no_keep_alive:
            output = subprocess.check_output(["ab"] + ab_parameters).decode('utf-8').split('\n')
        else:
            output = subprocess.check_output(["ab", "-k"] + ab_parameters).decode('utf-8').split('\n')

        failed_requests = 1
        complete_requests = 0
        for line in output:
            if 'Failed requests' in line:
                if len(line.split()) == 3:
                   failed_requests = int(line.split()[2])
                if failed_requests > 0:
                   print(line)
            elif 'Requests per second' in line:
                print(line)
            elif 'Complete requests' in line:
                if len(line.split()) == 3:
                   complete_requests = int(line.split()[2])
                print(line)

    print("------------")

    if expected_http_line != None:
        check_with_curl(app_url, expected_http_line)

    success = True
    try:
        app.kill()
        app.join()
    except Exception as ex:
        if error_line_to_ignore_on_kill != "" and error_line_to_ignore_on_kill in app.line_with_error():
            print("Ignorring error from guest on kill: %s" % app.line_with_error())
        else:
            print("  \033[91mERROR: Guest failed on kill() or join(): %s\033[00m" % str(ex))
            write_to_status_file("  ERROR: Guest failed on kill() or join(): %s" % str(ex))
            success = False

    if test_client == 'ab':
        if failed_requests > 0:
            print("  \033[91mFAILED ab - encountered failed requests: %d\033[00m" % failed_requests)
            write_to_status_file("  FAILED ab - encountered failed requests: %d" % failed_requests)
            success = False

        if complete_requests < count:
            print("  \033[91mFAILED ab - too few complete requests : %d ? %d\033[00m" % (complete_requests, count))
            write_to_status_file("  FAILED ab - too few complete requests : %d ? %d" % (complete_requests, count))
            success = False

    if success:
        print('----------')
        print('  \033[92mSUCCESS\033[00m')
        write_to_status_file('  SUCCESS')

if __name__ == "__main__":
    parser = argparse.ArgumentParser(prog='test_app')
    parser.add_argument("-i", "--image", action="store", default=None, metavar="IMAGE",
                        help="path to disk image file. defaults to build/$mode/usr.img")
    parser.add_argument("-p", "--hypervisor", action="store", default=None,
                        help="choose hypervisor to run: qemu, firecracker")
    parser.add_argument("--line", action="store", default=None,
                        help="expect line in guest output")
    parser.add_argument("--guest_port", action="store", help="guest port")
    parser.add_argument("--host_port", action="store", default=8000, help="host port")
    parser.add_argument("--http_line", action="store", default=None,
                        help="expect line in http output")
    parser.add_argument("--http_path", action="store", default='/',
                        help="http path")
    parser.add_argument("-e", "--execute", action="store", default='runscript /run/default;', metavar="CMD",
                        help="edit command line before execution")
    parser.add_argument("--concurrency", action="store", type=int, default=50, help="number of concurrent requests")
    parser.add_argument("--count", action="store", type=int, default=1000, help="total number of requests (ab specific)")
    parser.add_argument("--duration", action="store", type=int, default=10, help="duration of test in seconds (wrk specific)")
    parser.add_argument("--threads", action="store", type=int, default=4, help="duration of test in seconds (wrk specific)")
    parser.add_argument("--pre_script", action="store", default=None, help="path to a script that will be executed before the test")
    parser.add_argument("--no_keep_alive", action="store_true", help="do not use 'keep alive' flag - '-k' with ab")
    parser.add_argument("--error_line_to_ignore_on_kill", action="store", default='',
                        help="error line to ignore on kill")
    parser.add_argument("--kernel_path", action="store", help="path to kernel.elf.")
    parser.add_argument("--qemu_tap", action="store", default="qemu_tap0", help="QEMU tap device name")

    cmdargs = parser.parse_args()

    hypervisor_name = 'qemu'
    if cmdargs.hypervisor != None:
        hypervisor_name = cmdargs.hypervisor
    else:
        hypervisor_from_env = os.getenv('OSV_HYPERVISOR')
        if hypervisor_from_env != None:
            hypervisor_name = hypervisor_from_env

    if hypervisor_name == 'firecracker':
        os.environ['OSV_HOSTNAME'] = '172.16.0.2'
    else:
        os.environ['OSV_HOSTNAME'] = '127.0.0.1'

    kernel_path = cmdargs.kernel_path
    if not kernel_path and os.getenv('OSV_KERNEL'):
        kernel_path = os.getenv('OSV_KERNEL')

    if kernel_path and not os.path.exists(kernel_path):
        print("The file %s does not exist!" % kernel_path)
        sys.exit(-1)

    test_client = 'ab'
    if os.getenv('TESTER'):
        test_client = os.getenv('TESTER')

    if os.getenv('TESTER_CONCURRENCY'):
        cmdargs.concurrency = int(os.getenv('TESTER_CONCURRENCY'))

    if os.getenv('TESTER_COUNT'):
        cmdargs.count = int(os.getenv('TESTER_COUNT'))

    if os.getenv('TESTER_DURATION'):
        cmdargs.duration = int(os.getenv('TESTER_DURATION'))

    if os.getenv('TESTER_THREADS'):
        cmdargs.threads = int(os.getenv('TESTER_THREADS'))

    set_verbose_output(True)
    run(cmdargs.execute, hypervisor_name, cmdargs.host_port, cmdargs.guest_port,
       cmdargs.http_path ,cmdargs.http_line, cmdargs.image, cmdargs.line,
       cmdargs.concurrency, cmdargs.count, cmdargs.duration, cmdargs.threads,
       cmdargs.pre_script, cmdargs.no_keep_alive,
       cmdargs.error_line_to_ignore_on_kill, kernel_path, test_client, cmdargs.qemu_tap)
