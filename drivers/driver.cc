#include "drivers/driver.hh"
#include "debug.hh"

bool Driver::isPresent() {
    return false;
}

u16 Driver::getStatus() {
    return 0;
}

void Driver::setStatus(u16 s) {
}

bool
Driver::Init(Device* dev) {
    if (!dev) return false;

    debug(fmt("Driver:Init %x:%x") % _vid % _id);

    return true;
}

void Driver::dumpConfig() const {
    debug(fmt("Driver vid:id= %x:%x") % _vid % _id);
}

std::ostream& operator << (std::ostream& out, const Driver& d) {
   out << "Driver dev id=" << d._id << " vid=" << d._vid << std::endl;
   return out;
}
