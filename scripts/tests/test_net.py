from tests.testing import *
import socket
import errno

def is_broken_pipe_error(e):
    return isinstance(e, IOError) and e.errno == errno.EPIPE

def tcp_close_without_reading(hypervisor, host_name):
    host_port = 7777
    server = run_command_in_guest('/tests/misc-tcp-close-without-reading.so',
        forward=[(host_port, 7777)], hypervisor=hypervisor)

    wait_for_line(server, 'listening...')

    s = socket.socket(socket.AF_INET, socket.SOCK_STREAM)
    s.connect((host_name, host_port))
    try:
        while server.is_alive():
            s.sendall(b'.' * 1024)
    except Exception as e:
        if not is_broken_pipe_error(e):
            raise

    server.join()

@test
def tcp_close_without_reading_on_qemu():
    tcp_close_without_reading('qemu', 'localhost')

@test
def tcp_close_without_reading_on_firecracker():
    tcp_close_without_reading('firecracker', '172.16.0.2')
