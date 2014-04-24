from testing import *
import socket
import errno
import os
import subprocess

def is_broken_pipe_error(e):
    return isinstance(e, IOError) and e.errno == errno.EPIPE

@test
def tracing_smoke_test():
    path = '/this/path/does/not/exist'
    guest = Guest(['--trace=vfs_*,net_packet*', '-e', path], hold_with_poweroff=True, show_output_on_error=False)
    try:
        wait_for_line(guest, 'run_main(): cannot execute %s. Powering off.' % path)

        trace_script = os.path.join(osv_base, 'scripts', 'trace.py')

        if os.path.exists('tracefile'):
            os.remove('tracefile')

        subprocess.check_output([trace_script, 'extract'])

        summary = subprocess.check_output([trace_script, 'summary'])
        assert('vfs_open' in summary)
        assert('vfs_open_err' in summary)

        samples = subprocess.check_output([trace_script, 'list'])
        assert('vfs_open             "%s" 0x0 00' % path in samples)
        assert('vfs_open_err         2' in samples)

        tcpdump = subprocess.check_output([trace_script, 'tcpdump'])
        assert('0.0.0.0.68 > 255.255.255.255.67: BOOTP/DHCP, Request from' in tcpdump)
        assert('192.168.122.1.67 > 255.255.255.255.68: BOOTP/DHCP, Reply' in tcpdump)

        tcpdump = subprocess.check_output([trace_script, 'list', '--tcpdump'])
        assert('0.0.0.0.68 > 255.255.255.255.67: BOOTP/DHCP, Request from' in tcpdump)
        assert('192.168.122.1.67 > 255.255.255.255.68: BOOTP/DHCP, Reply' in tcpdump)
    finally:
        guest.kill()
