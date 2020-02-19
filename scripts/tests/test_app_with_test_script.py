#!/usr/bin/python3
from testing import *
import argparse
import runpy

def run(command, hypervisor_name, host_port, guest_port, script_path, image_path=None, start_line=None, end_line=None, use_vhost_networking=False):
    py_args = []
    if image_path != None:
        py_args = ['--image', image_path]

    if use_vhost_networking and hypervisor_name != 'firecracker':
        app = run_command_in_guest(command, hypervisor=hypervisor_name, run_py_args=py_args + ['-nv'])
    else:
        app = run_command_in_guest(command, hypervisor=hypervisor_name, run_py_args=py_args, forward=[(host_port, guest_port)])

    if start_line != None:
        wait_for_line_contains(app, start_line)

    print("-----------------------------------")
    script_out = runpy.run_path(script_path)

    if end_line != None:
        wait_for_line_contains(app, end_line)

    app.kill()
    app.join()

    print("-----------------------------------")
    if script_out['success'] == True:
        print("SUCCESS")
    else:
        print("FAILURE")

if __name__ == "__main__":
    parser = argparse.ArgumentParser(prog='test_app')
    parser.add_argument("-i", "--image", action="store", default=None, metavar="IMAGE",
                        help="path to disk image file. defaults to build/$mode/usr.img")
    parser.add_argument("-p", "--hypervisor", action="store", default=None,
                        help="choose hypervisor to run: qemu, firecracker")
    parser.add_argument("--start_line", action="store", default=None,
                        help="expect line in guest output before executing test script")
    parser.add_argument("--end_line", action="store", default=None,
                        help="expect line in guest output after test completes")
    parser.add_argument("--host_port", action="store", default = 8000, help="host port")
    parser.add_argument("--guest_port", action="store", default = 8000, help="guest port")
    parser.add_argument("--script_path", action="store", default='/',
                        help="path to test script path")
    parser.add_argument("-e", "--execute", action="store", default='runscript /run/default;', metavar="CMD",
                        help="edit command line before execution")
    parser.add_argument("-n", "--vhost", action="store_true", help="setup tap/vhost networking")

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
    elif cmdargs.vhost:
        os.environ['OSV_HOSTNAME'] = '192.168.122.76'
    else:
        os.environ['OSV_HOSTNAME'] = 'localhost'

    set_verbose_output(True)
    run(cmdargs.execute, hypervisor_name, cmdargs.host_port, cmdargs.guest_port, cmdargs.script_path, cmdargs.image, cmdargs.start_line, cmdargs.end_line, cmdargs.vhost)
