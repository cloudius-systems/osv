#include "drivers/driver-factory.hh"
#include "drivers/driver.hh"
#include "debug.hh"

DriverFactory* DriverFactory::pinstance = 0;

void
DriverFactory::RegisterDriver(Driver* d) {
    if (d) _drivers.insert(d);
}

Driver*
DriverFactory::lookup(u16 id, u16 vid) {
    Driver dummy(id, vid);
    Driver::equal compare;

    for (auto ii = _drivers.begin() ; ii != _drivers.end() ; ii++ )
         if (compare(&dummy, *ii)) return *ii;

    return nullptr;
}

bool
DriverFactory::InitializeDriver(Device* dev) {
    if (!dev) return false;

    Driver *drv = lookup(dev->getvid(), dev->getid());
    if (!drv) return false;

    drv->setPresent(dev->getBus(), dev->getSlot(), dev->getFunc());
    return drv->Init(dev);
}


void
DriverFactory::DumpDrivers() {
    for (auto ii = _drivers.begin() ; ii != _drivers.end() ; ii++ )
         (*ii)->dumpConfig();
}

void
DriverFactory::Destroy() {
    for (auto ii = _drivers.begin() ; ii != _drivers.end() ; ii++ ) {
        Driver* del_me = *ii;
        ii = _drivers.erase(ii);
        delete del_me;
    }
}
