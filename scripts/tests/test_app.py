#!/usr/bin/python3
from testing import *
import argparse
from time import sleep

def run(command, hypervisor_name, image_path=None, line=None, guest_port=None, host_port=None, input_lines=[], kill_app=False):
    py_args = []
    if image_path != None:
        py_args = ['--image', image_path]

    pipe_guest_stdin= len(input_lines) > 0
    if guest_port != None and host_port != None:
        app = run_command_in_guest(command, hypervisor=hypervisor_name, run_py_args=py_args, forward=[(host_port, guest_port)], pipe_stdin=pipe_guest_stdin)
    else:
        app = run_command_in_guest(command, hypervisor=hypervisor_name, run_py_args=py_args, pipe_stdin=pipe_guest_stdin)

    if line != None:
        if len(input_lines) > 0:
            app.write_line_to_input(">")
        wait_for_line_contains(app, line)

    for line in input_lines:
        app.write_line_to_input(line)

    if kill_app:
       app.kill()

    app.join()

    print('----------')
    print('SUCCESS')

if __name__ == "__main__":
    parser = argparse.ArgumentParser(prog='test_app')
    parser.add_argument("-i", "--image", action="store", default=None, metavar="IMAGE",
                        help="path to disk image file. defaults to build/$mode/usr.img")
    parser.add_argument("-p", "--hypervisor", action="store", default=None,
                        help="choose hypervisor to run: qemu, firecracker")
    parser.add_argument("--line", action="store", default=None,
                        help="expect line in guest output")
    parser.add_argument("-e", "--execute", action="store", default='runscript /run/default;', metavar="CMD",
                        help="edit command line before execution")
    parser.add_argument("--guest_port", action="store", default=None, help="guest port")
    parser.add_argument("--host_port", action="store", default=None, help="host port")
    parser.add_argument("--input_line", action="append", default=[], help="input line")
    parser.add_argument("--kill", action="store_true", help="kill the app instead of waiting until terminates itself")

    cmdargs = parser.parse_args()

    hypervisor_name = 'qemu'
    if cmdargs.hypervisor != None:
        hypervisor_name = cmdargs.hypervisor
    else:
        hypervisor_from_env = os.getenv('OSV_HYPERVISOR')
        if hypervisor_from_env != None:
            hypervisor_name = hypervisor_from_env

    set_verbose_output(True)
    run(cmdargs.execute, hypervisor_name, cmdargs.image, cmdargs.line,
        cmdargs.guest_port, cmdargs.host_port, cmdargs.input_line, cmdargs.kill)
