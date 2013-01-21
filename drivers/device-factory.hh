#ifndef FACTORY_H
#define FACTORY_H

#include "drivers/pci.hh"
#include "drivers/pci-function.hh"
#include <unordered_set>

using namespace pci;

class DeviceFactory {
public:
    static DeviceFactory* Instance() {return (pinstance)? pinstance: (pinstance = new DeviceFactory);};
    void AddDevice(u8 bus, u8 slot, u8 func);

    void DumpDevices();
    void InitializeDrivers();

private:
   DeviceFactory() {pinstance = 0;};
   DeviceFactory(const DeviceFactory& f) {};
   DeviceFactory& operator=(DeviceFactory& f) {pinstance = f.pinstance; return *pinstance;};

   static DeviceFactory* pinstance;
   std::unordered_set<pci_function*, pci_function::hash, pci_function::equal> _devices;
};

#endif
