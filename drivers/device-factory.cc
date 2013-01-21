#include "drivers/device-factory.hh"
#include "drivers/driver-factory.hh"
#include "drivers/pci-function.hh"
#include "drivers/pci-device.hh"
#include "drivers/pci-bridge.hh"
#include "debug.hh"

DeviceFactory* DeviceFactory::pinstance = 0;

void
DeviceFactory::AddDevice(u8 bus, u8 slot, u8 func) {
	// Add just the devices for now
	if (pci_function::is_device(bus, slot, func)) {
		pci_function* dev = new pci_device(bus, slot, func);
		dev->parse_pci_config();
		_devices.insert(dev);
	}
}

void
DeviceFactory::DumpDevices() {
	debug("-----=[ Dumping Devices ]=-----");
    for (auto ii = _devices.begin() ; ii != _devices.end() ; ii++ )
         (*ii)->dump_config();
    debug("-----=[ End Dumping Devices ]=-----");

}

void
DeviceFactory::InitializeDrivers() {
    for (auto ii = _devices.begin() ; ii != _devices.end() ; ii++ )
        DriverFactory::Instance()->InitializeDriver((pci_device *)*ii);
}
