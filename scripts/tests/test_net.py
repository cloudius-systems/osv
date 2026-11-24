from tests.testing import *
import socket
import errno, os

arch = os.uname().machine
def set_arch(_arch):
    global arch
    arch = _arch

def is_broken_pipe_error(e):
    return isinstance(e, IOError) and e.errno == errno.EPIPE

def tcp_close_without_reading(hypervisor, host_name):
    global arch
    host_port = 7777
    run_args = []
    if os.uname().machine != arch:
        run_args=['--arch', arch]
    server = run_command_in_guest('/tests/misc-tcp-close-without-reading.so',
        forward=[(host_port, 7777)], hypervisor=hypervisor, run_py_args=run_args)

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

# Overflow a listen queue so that syncache_expand() fails to create sockets,
# then check that new connections through the NIC still complete.  If a
# failed expansion leaves its SYN cache bucket locked, the receive thread
# blocks on that bucket and the guest stops answering.  The bucket index
# is a hash of the ports in network byte order, masked to 9 bits: the
# port's high byte and the low bit of its low byte.  Vary both, so the
# host's connections reach almost every bucket.
def tcp_syncache_flood(hypervisor, host_name):
    global arch
    host_port = 7778
    source_ports = [hi << 8 | lo for hi in range(4, 256) for lo in (0, 1)]
    run_args = []
    if os.uname().machine != arch:
        run_args=['--arch', arch]
    server = run_command_in_guest('/tests/misc-tcp-syncache-flood.so',
        forward=[(host_port, 7778)], hypervisor=hypervisor, run_py_args=run_args)

    try:
        wait_for_line_starts(server, 'flooded')
        for source_port in source_ports:
            s = socket.socket(socket.AF_INET, socket.SOCK_STREAM)
            s.setsockopt(socket.SOL_SOCKET, socket.SO_REUSEADDR, 1)
            s.settimeout(5)
            try:
                s.bind(('', source_port))
            except OSError as e:
                if e.errno != errno.EADDRINUSE:
                    raise
                s.close()
                continue
            s.connect((host_name, host_port))
            reply = s.recv(2)
            s.close()
            if reply != b'ok':
                raise Exception('source port %d got %r instead of ok' % (source_port, reply))
    finally:
        server.kill()

@test
def tcp_syncache_flood_on_qemu():
    tcp_syncache_flood('qemu', 'localhost')

@test
def tcp_close_without_reading_on_qemu():
    tcp_close_without_reading('qemu', 'localhost')

@test
def tcp_close_without_reading_on_fc():
    tcp_close_without_reading('firecracker', '172.16.0.2')
