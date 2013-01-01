#ifndef VIRTIO_DRIVER_H
#define VIRTIO_DRIVER_H

#include "arch/x64/processor.hh"
#include "drivers/pci.hh"
#include "drivers/driver.hh"

class Virtio : public Driver {
public:
    enum {
        VIRTIO_VENDOR_ID = 0x1af4,
        VIRTIO_PCI_ABI_VERSION = 0x0,
        VIRTIO_PCI_ID_MIN = 0x1000,
        VIRTIO_PCI_ID_MAX = 0x103f,
    };

    Virtio(u16 id) : Driver(VIRTIO_VENDOR_ID, id) {};
    virtual void dumpConfig() const;
    virtual bool Init(Device *d);

protected:

    virtual bool earlyInitChecks();


private:
};

#endif
