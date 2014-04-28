from testing import *
import socket
import errno

def is_broken_pipe_error(e):
    return isinstance(e, IOError) and e.errno == errno.EPIPE

@test
def tcp_close_without_reading():
    host_port = 7777
    server = run_command_in_guest('/tests/misc-tcp-close-without-reading.so',
        forward=[(host_port, 7777)])

    wait_for_line(server, 'listening...')

    s = socket.socket(socket.AF_INET, socket.SOCK_STREAM)
    s.connect(('localhost', host_port))
    try:
        while server.is_alive():
            s.sendall('.' * 1024)
    except Exception, e:
        if not is_broken_pipe_error(e):
            raise

    server.join()
