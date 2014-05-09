from testing import *
import os
import subprocess

@test
def tracing_smoke_test():
    path = '/this/path/does/not/exist'
    guest = Guest(['--trace=vfs_*,net_packet*,sched_wait*', '--trace-backtrace', '-e', path],
        hold_with_poweroff=True, show_output_on_error=False)
    try:
        wait_for_line(guest, 'run_main(): cannot execute %s. Powering off.' % path)

        trace_script = os.path.join(osv_base, 'scripts', 'trace.py')

        if os.path.exists('tracefile'):
            os.remove('tracefile')

        assert(subprocess.call([trace_script, 'extract']) == 0)

        summary_output = subprocess.check_output([trace_script, 'summary', '--timed'])
        summary, timed_summary = summary_output.split('Timed tracepoints')

        assert('vfs_open' in summary)
        assert('vfs_open_err' in summary)

        assert('vfs_pwritev' in timed_summary)
        assert('vfs_open' in timed_summary)

        samples = subprocess.check_output([trace_script, 'list'])
        assert('vfs_open             "%s" 0x0 00' % path in samples)
        assert('vfs_open_err         2' in samples)

        samples = subprocess.check_output([trace_script, 'list-timed'])
        assert('vfs_open             "%s" 0x0 00' % path in samples)

        profile = subprocess.check_output([trace_script, 'prof-timed', '-t', 'vfs_open'])
        assert('open' in profile)
        assert('elf::program::get_library' in profile)

        profile = subprocess.check_output([trace_script, 'prof-wait'])
        assert('osv::poweroff' in profile)

        profile = subprocess.check_output([trace_script, 'prof'])
        assert('osv::poweroff' in profile)
        assert('elf::program::get_library' in profile)

        tcpdump = subprocess.check_output([trace_script, 'tcpdump'])
        assert('0.0.0.0.68 > 255.255.255.255.67: BOOTP/DHCP, Request from' in tcpdump)
        assert('192.168.122.1.67 > 255.255.255.255.68: BOOTP/DHCP, Reply' in tcpdump)

        tcpdump = subprocess.check_output([trace_script, 'list', '--tcpdump'])
        assert('0.0.0.0.68 > 255.255.255.255.67: BOOTP/DHCP, Request from' in tcpdump)
        assert('192.168.122.1.67 > 255.255.255.255.68: BOOTP/DHCP, Reply' in tcpdump)
    finally:
        guest.kill()
