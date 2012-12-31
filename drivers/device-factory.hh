#ifndef FACTORY_H
#define FACTORY_H

#include "drivers/pci.hh"
#include "drivers/device.hh"
#include <unordered_set>

using namespace pci;

class DeviceFactory {
public:
    static DeviceFactory* Instance() {return (pinstance)? pinstance: (pinstance = new DeviceFactory);};
    void AddDevice(u8 bus, u8 slot, u8 func);

    void DumpDevices();

private:
   DeviceFactory() {pinstance = 0;};
   DeviceFactory(const DeviceFactory& f) {};
   DeviceFactory& operator=(const DeviceFactory& f) {pinstance = f.pinstance; return *pinstance;};

   static DeviceFactory* pinstance;
   std::unordered_set<const Device*, Device::hash, Device::equal> _devices;
};

#endif
