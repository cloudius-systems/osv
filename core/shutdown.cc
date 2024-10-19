#include <osv/shutdown.hh>
#include <osv/power.hh>
#include <osv/debug.hh>
#include <osv/sched.hh>
#include <osv/dhcp.hh>
#include <osv/strace.hh>

extern void vfs_exit(void);

namespace osv {

void shutdown()
{
#if CONF_tracepoints
    wait_strace_complete();
#endif
    dhcp_release();

    // The vfs_exit() call below will forcibly unmount the filesystem. If any
    // thread is executing code mapped from a file, these threads may crash if
    // they happen to run between the call to vfs_exit() and the call to
    // osv::poweroff() - see issue #580.
    // As an attempt to avoid this problem, we stop here all application
    // threads. Unfortunately, this is NOT a completely reliable solution,
    // as stopping a thread while it holds an important OSv mutex can cause
    // the entire system to deadlock. Hopefully, in *most* cases, reasonable
    // applications will not call exit() while their other threads are doing
    // important work. But eventually, we need a better solution here.
    bool stopped_others = false;
    auto current = sched::thread::current();
    while (!stopped_others) {
        stopped_others = true;
        sched::with_all_threads([&](sched::thread &t) {
            if (&t != current && t.is_app()) {
                stopped_others &= t.unsafe_stop();
            }
        });
    }

    vfs_exit();
    debug("Powering off.\n");
    osv::poweroff();
}

}
