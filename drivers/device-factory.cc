#include "drivers/device-factory.hh"
#include "drivers/driver-factory.hh"
#include "drivers/device.hh"
#include "debug.hh"

DeviceFactory* DeviceFactory::pinstance = 0;

void
DeviceFactory::AddDevice(u8 bus, u8 slot, u8 func) {
    // read device id and vendor id
    u32 ids = read_pci_config(bus, slot, func, 0);
    _devices.insert(new Device(ids>>16, ids & 0xffff));
}

void
DeviceFactory::DumpDevices() {
    for (auto ii = _devices.begin() ; ii != _devices.end() ; ii++ )
         (*ii)->dumpConfig();
}

void
DeviceFactory::InitializeDrivers() {
    for (auto ii = _devices.begin() ; ii != _devices.end() ; ii++ )
        DriverFactory::Instance()->InitializeDriver(*ii);
}
