#!/usr/bin/python
from testing import *
import argparse
import subprocess
from time import sleep

def check_with_curl(url, expected_http_line):
    output = subprocess.check_output(["curl", "-s", url])
    print(output)
    if expected_http_line not in output:
       print("FAILED curl: wrong output")
    print("------------")

def run(command, hypervisor_name, host_port, guest_port, http_path, expected_http_line=None,
        image_path=None, line=None, concurrency=50, count=1000, pre_script=None,
        no_keep_alive=False, error_line_to_ignore_on_kill = ""):

    py_args = []
    if image_path != None:
        py_args = ['--image', image_path]

    app = run_command_in_guest(command, hypervisor=hypervisor_name, run_py_args=py_args, forward=[(host_port, guest_port)])

    if line != None:
        wait_for_line_contains(app, line)

    sleep(0.05)

    if pre_script != None:
       print(pre_script)
       subprocess.check_output([pre_script])

    app_url = "http://localhost:%s%s" % (host_port, http_path)
    if expected_http_line != None:
        check_with_curl(app_url, expected_http_line)

    if no_keep_alive:
        output = subprocess.check_output(["ab", "-l", "-c", str(concurrency), "-n", str(count), app_url]).split('\n')
    else:
        output = subprocess.check_output(["ab", "-l", "-k", "-c", str(concurrency), "-n", str(count), app_url]).split('\n')

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
            print("ERROR: Guest failed on kill() or join(): %s" % str(ex))
            success = False

    if failed_requests > 0:
        print("FAILED ab - encountered failed requests: %d" % failed_requests) 
        success = False

    if complete_requests < count:
        print("FAILED ab - too few complete requests : %d ? %d" % (complete_requests, count)) 
        success = False

    if success:
        print('----------')
        print('SUCCESS')

if __name__ == "__main__":
    parser = argparse.ArgumentParser(prog='test_app')
    parser.add_argument("-i", "--image", action="store", default=None, metavar="IMAGE",
                        help="path to disk image file. defaults to build/$mode/usr.img")
    parser.add_argument("-p", "--hypervisor", action="store", default="qemu", 
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
    parser.add_argument("--count", action="store", type=int, default=1000, help="total number of requests")
    parser.add_argument("--pre_script", action="store", default=None, help="path to a script that will be executed before the test")
    parser.add_argument("--no_keep_alive", action="store_true", help="do not use 'keep alive' flag - '-k' with ab")
    parser.add_argument("--error_line_to_ignore_on_kill", action="store", default='',
                        help="error line to ignore on kill")

    cmdargs = parser.parse_args()
    set_verbose_output(True)
    run(cmdargs.execute, cmdargs.hypervisor, cmdargs.host_port, cmdargs.guest_port,
       cmdargs.http_path ,cmdargs.http_line, cmdargs.image, cmdargs.line,
       cmdargs.concurrency, cmdargs.count, cmdargs.pre_script, cmdargs.no_keep_alive, cmdargs.error_line_to_ignore_on_kill)
