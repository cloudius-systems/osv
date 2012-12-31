#include "drivers/device.hh"
#include "debug.hh"

bool Device::isPresent() {
    return false;
}

u16 Device::getStatus() {
    return 0;
}

void Device::setStatus(u16 s) {
}

void Device::dumpConfig() const {
    debug(fmt("device vid:id= %x:%x") % _vid % _id);
}

std::ostream& operator << (std::ostream& out, const Device& d) {
   out << "driver dev id=" << d._id << " vid=" << d._vid << std::endl;
   return out;
}
