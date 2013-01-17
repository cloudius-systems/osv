#ifndef DRIVER_FACTORY_H
#define DRIVER_FACTORY_H

#include "drivers/pci.hh"
#include "drivers/driver.hh"
#include "drivers/device.hh"
#include <unordered_set>

using namespace pci;

class DriverFactory {
public:
    static DriverFactory* Instance() {return (pinstance)? pinstance: (pinstance = new DriverFactory);};
    void RegisterDriver(Driver* d);
    Driver* lookup(u16 id, u16 vid);
    bool InitializeDriver(Device* d);

    void DumpDrivers();

    void Destroy();

private:
   DriverFactory() {pinstance = 0;};
   DriverFactory(const DriverFactory& f) {};
   DriverFactory& operator=(const DriverFactory& f) {pinstance = f.pinstance; return *pinstance;};

   static DriverFactory* pinstance;
   std::unordered_set<Driver*, Driver::hash, Driver::equal> _drivers;
};

#endif
