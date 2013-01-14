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

    debug(fmt("%s passed. Subsystem: vid:%x:id:%x") % __FUNCTION__ % (u16)getSubsysVid() % (u16)getSubsysId());
    return true;
}

bool
Virtio::Init(Device* dev) {

    if (!earlyInitChecks()) return false;

    if (!Driver::Init(dev)) return false;

    debug(fmt("Virtio:Init %x:%x") % _vid % _id);

    add_dev_status(VIRTIO_CONFIG_S_ACKNOWLEDGE | VIRTIO_CONFIG_S_DRIVER);

    probe_virt_queues();

    for (int i=0;i<32;i++)
        debug(fmt("%d:%d ") % i % get_device_feature_bit(i), false);
    debug(fmt("\n"), false);

    return true;
}

void Virtio::dumpConfig() const {
    Driver::dumpConfig();
    debug(fmt("Virtio vid:id= %x:%x") % _vid % _id);
}


u32
Virtio::get_device_features(void) {
    return (get_virtio_config(VIRTIO_PCI_HOST_FEATURES));
}

bool
Virtio::get_device_feature_bit(int bit) {
    return (get_virtio_config_bit(VIRTIO_PCI_HOST_FEATURES, bit));
}

void
Virtio::set_guest_features(u32 features) {
    set_virtio_config(VIRTIO_PCI_GUEST_FEATURES, features);
}

void
Virtio::set_guest_feature_bit(int bit, bool on) {
    set_virtio_config_bit(VIRTIO_PCI_GUEST_FEATURES, bit, on);
}

u32
Virtio::get_dev_status(void) {
    return (get_virtio_config(VIRTIO_PCI_STATUS));
}

void
Virtio::set_dev_status(u32 status) {
    set_virtio_config(VIRTIO_PCI_STATUS, status);
}

void
Virtio::add_dev_status(u32 status) {
    set_dev_status(get_dev_status() | status);
}

void
Virtio::del_dev_status(u32 status) {
    set_dev_status(get_dev_status() & ~status);
}

u32 
Virtio::get_virtio_config(int offset) {
    return (_bars[0]->read(offset));
}

void
Virtio::set_virtio_config(int offset, u32 val) {
    _bars[0]->write(offset, val);
}

bool
Virtio::get_virtio_config_bit(int offset, int bit){
    return (get_virtio_config(offset) & (1 << bit));
}

void
Virtio::set_virtio_config_bit(int offset, int bit, bool on) {
    u32 val = get_virtio_config(offset);
    u32 newval = ( val & ~(1 << bit) ) | ((int)(on)<<bit);
    set_virtio_config(offset, newval);
}


void
Virtio::pci_conf_write(int offset, void* buf, int length) {
    u8* ptr = reinterpret_cast<u8*>(buf);
    for (int i=0;i<length;i++)
        _bars[0]->write(offset+i, ptr[i]);
}

void
Virtio::pci_conf_read(int offset, void* buf, int length) {
    unsigned char* ptr = reinterpret_cast<unsigned char*>(buf);
    for (int i=0;i<length;i++)
        ptr[i] = _bars[0]->readb(offset+i);
}

void
Virtio::probe_virt_queues() {
    u16 queuesel = 0;
    u16 qsize;

    do {
        pci_conf_write(VIRTIO_PCI_QUEUE_SEL, queuesel);
        qsize = pci_conf_readw(VIRTIO_PCI_QUEUE_NUM);
        debug(fmt("queue %d, size %d") % queuesel % qsize);

        if (!qsize) break;
        queuesel++;
    } while (1);
}
