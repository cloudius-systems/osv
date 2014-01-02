#include <osv/shutdown.hh>
#include <osv/power.hh>
#include "debug.hh"

extern "C" {
    void unmount_rootfs();
}

namespace osv {

void shutdown()
{
    unmount_rootfs();
    debug("Powering off.\n");
    osv::poweroff();
}

}
