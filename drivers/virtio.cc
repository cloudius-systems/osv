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

    _bars[0]->write(VIRTIO_PCI_STATUS, (u8)(VIRTIO_CONFIG_S_ACKNOWLEDGE |
            VIRTIO_CONFIG_S_DRIVER));

    probe_virt_queues();

    for (int i=0;i<32;i++)
        debug(fmt("%d:%d ") % i % get_device_feature_bit(i), false);
    debug(fmt("\n"), false);

    _bars[0]->write(VIRTIO_PCI_STATUS, (u8)(VIRTIO_CONFIG_S_DRIVER_OK));

    return true;
}

void Virtio::dumpConfig() const {
    Driver::dumpConfig();
    debug(fmt("Virtio vid:id= %x:%x") % _vid % _id);
}

void
Virtio::vring_init(struct vring *vr, unsigned int num, void *p, unsigned long align) {
    vr->num = num;
    vr->desc = reinterpret_cast<struct vring_desc*>(p);
    vr->avail = reinterpret_cast<struct vring_avail*>(p) + num*sizeof(struct vring_desc);
    vr->used = reinterpret_cast<struct vring_used*>(((unsigned long)&vr->avail->ring[num] + sizeof(u16) \
        + align-1) & ~(align - 1));
}

unsigned
Virtio::vring_size(unsigned int num, unsigned long align) {
    return ((sizeof(struct vring_desc) * num + sizeof(u16) * (3 + num)
         + align - 1) & ~(align - 1))
        + sizeof(u16) * 3 + sizeof(struct vring_used_elem) * num;
}

/* The following is used with USED_EVENT_IDX and AVAIL_EVENT_IDX */
/* Assuming a given event_idx value from the other size, if
 * we have just incremented index from old to new_idx,
 * should we trigger an event? */
int
Virtio::vring_need_event(u16 event_idx, u16 new_idx, u16 old) {
    /* Note: Xen has similar logic for notification hold-off
     * in include/xen/interface/io/ring.h with req_event and req_prod
     * corresponding to event_idx + 1 and new_idx respectively.
     * Note also that req_event and req_prod in Xen start at 1,
     * event indexes in virtio start at 0. */
    return (u16)(new_idx - event_idx - 1) < (u16)(new_idx - old);
}

bool
Virtio::get_device_feature_bit(int bit) {
    u32 features = _bars[0]->read(VIRTIO_PCI_HOST_FEATURES);
    return bool(features & (1 << bit));
}

void
Virtio::set_guest_feature_bit(int bit, bool on) {
    u32 features = _bars[0]->read(VIRTIO_PCI_GUEST_FEATURES);
    features = (on)? features | (1 << bit) : features & ~(1 << bit);
    _bars[0]->write(VIRTIO_PCI_GUEST_FEATURES, features);
}

void
Virtio::set_guest_features(u32 features) {
    _bars[0]->write(VIRTIO_PCI_GUEST_FEATURES, features);
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
