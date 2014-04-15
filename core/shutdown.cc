#include <osv/shutdown.hh>
#include <osv/power.hh>
#include <osv/debug.hh>

extern void vfs_exit(void);

namespace osv {

void shutdown()
{
    vfs_exit();
    debug("Powering off.\n");
    osv::poweroff();
}

}
