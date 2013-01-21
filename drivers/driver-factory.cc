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
DriverFactory::InitializeDriver(pci_device* dev) {
    if (!dev) return false;
    Driver *drv = lookup(dev->get_vendor_id(), dev->get_device_id());
    if (!drv) return false;
    return drv->Init(dev);
}


void
DriverFactory::DumpDrivers() {
	debug("-----=[ Dumping Drivers ]=-----");
    for (auto ii = _drivers.begin() ; ii != _drivers.end() ; ii++ )
         (*ii)->dump_config();
}

void
DriverFactory::Destroy() {
    for (auto ii = _drivers.begin() ; ii != _drivers.end() ; ii++ ) {
        Driver* del_me = *ii;
        ii = _drivers.erase(ii);
        delete del_me;
    }
}
