#include "drivers/virtio.hh"
#include "debug.hh"

using namespace pci;

bool
Virtio::earlyInitChecks() {
    if (!Driver::earlyInitChecks()) return false;

    u8 rev;
    if (getRevision() != VIRTIO_PCI_ABI_VERSION) {
        debug(fmt("Wrong virtio revision=%x") % rev);
        return false;
    }

    if (_id < VIRTIO_PCI_ID_MIN || _id > VIRTIO_PCI_ID_MAX) {
        debug(fmt("Wrong virtio dev id %x") % _id);

        return false;
    }

    debug(fmt("%s passed") % __FUNCTION__);
    return true;
}

bool
Virtio::Init(Device* dev) {
    if (!Driver::Init(dev)) return false;

    if (!earlyInitChecks()) return false;

    debug(fmt("Virtio:Init %x:%x") % _vid % _id);

    return true;
}

void Virtio::dumpConfig() const {
    Driver::dumpConfig();
    debug(fmt("Virtio vid:id= %x:%x") % _vid % _id);
}
