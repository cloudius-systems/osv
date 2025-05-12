/*
 * Copyright (C) 2019 Waldemar Kozaczuk.
 *
 * This work is open source software, licensed under the terms of the
 * BSD license as described in the LICENSE file in the top-level directory.
 */

#include "drivers/virtio-pci-device.hh"
#include <algorithm>

namespace virtio {

virtio_pci_device::virtio_pci_device(pci::device *dev)
    : virtio_device()
    , _dev(dev)
    , _msi(dev)
{
}

virtio_pci_device::~virtio_pci_device() {
    delete _dev;
}

void virtio_pci_device::dump_config()
{
    u8 B, D, F;
    _dev->get_bdf(B, D, F);

    virtio_d("version:%s, type id:%d, pci device [%x:%x.%x] vid:id=%x:%x",
             get_version(), get_type_id(),
             (u16)B, (u16)D, (u16)F,
             _dev->get_vendor_id(),
             _dev->get_device_id());
}

void virtio_pci_device::init()
{
    bool status = parse_pci_config();
    assert(status);

    _dev->set_bus_master(true);

    _dev->msix_enable();
}

void virtio_pci_device::register_interrupt(interrupt_factory irq_factory)
{
    if (irq_factory.register_msi_bindings && _dev->is_msix()) {
        irq_factory.register_msi_bindings(_msi);
    } else {
        _irq.reset(irq_factory.create_pci_interrupt(*_dev));
    }
}

virtio_legacy_pci_device::virtio_legacy_pci_device(pci::device *dev)
    : virtio_pci_device(dev)
{
}

void virtio_legacy_pci_device::kick_queue(int queue)
{
    virtio_conf_writew(VIRTIO_PCI_QUEUE_NOTIFY, queue);
}

void virtio_legacy_pci_device::setup_queue(vring *queue)
{
    if (_dev->is_msix()) {
        // Setup queue_id:entry_id 1:1 correlation...
        virtio_conf_writew(VIRTIO_MSI_QUEUE_VECTOR, queue->index());
        if (virtio_conf_readw(VIRTIO_MSI_QUEUE_VECTOR) != queue->index()) {
            virtio_e("Setting MSIx entry for queue %d failed.", queue->index());
            return;
        }
    }
    // Tell host about pfn
    // TODO: Yak, this is a bug in the design, on large memory we'll have PFNs > 32 bit
    // Dor to notify Rusty
    virtio_conf_writel(VIRTIO_PCI_QUEUE_PFN, (u32)(queue->get_paddr() >> VIRTIO_PCI_QUEUE_ADDR_SHIFT));
}

void virtio_legacy_pci_device::select_queue(int queue)
{
    virtio_conf_writew(VIRTIO_PCI_QUEUE_SEL, queue);
}

u16 virtio_legacy_pci_device::get_queue_size()
{
    return virtio_conf_readw(VIRTIO_PCI_QUEUE_NUM);
}

bool virtio_legacy_pci_device::get_virtio_config_bit(u32 offset, int bit)
{
    return (virtio_conf_readl(offset) & (1 << bit)) != 0;
}

u64 virtio_legacy_pci_device::get_available_features()
{
    return virtio_conf_readl(VIRTIO_PCI_HOST_FEATURES);
}

void virtio_legacy_pci_device::set_enabled_features(u64 features)
{
    virtio_conf_writel(VIRTIO_PCI_GUEST_FEATURES, (u32)features);
}

u8 virtio_legacy_pci_device::get_status()
{
    return virtio_conf_readb(VIRTIO_PCI_STATUS);
}

void virtio_legacy_pci_device::set_status(u8 status)
{
    virtio_conf_writeb(VIRTIO_PCI_STATUS, status);
}

u8 virtio_legacy_pci_device::read_config(u32 offset)
{
    auto conf_start = _dev->is_msix_enabled()? 24 : 20;
    return _bar1->readb(conf_start + offset);
}

u8 virtio_legacy_pci_device::read_and_ack_isr()
{
    return virtio_conf_readb(VIRTIO_PCI_ISR);
}

bool virtio_legacy_pci_device::parse_pci_config()
{
    // Test whether bar1 is present
    _bar1 = _dev->get_bar(1);
    if (_bar1 == nullptr) {
        return false;
    }

    // Check ABI version
    u8 rev = _dev->get_revision_id();
    if (rev != VIRTIO_PCI_LEGACY_ABI_VERSION) {
        virtio_e("Wrong virtio revision=%x", rev);
        return false;
    }

    // Check device ID
    u16 dev_id = _dev->get_device_id();
    if ((dev_id < VIRTIO_PCI_LEGACY_ID_MIN) || (dev_id > VIRTIO_PCI_LEGACY_ID_MAX)) {
        virtio_e("Wrong virtio dev id %x", dev_id);
        return false;
    }

    return true;
}

virtio_modern_pci_device::virtio_modern_pci_device(pci::device *dev)
    : virtio_pci_device(dev)
{
}

void virtio_modern_pci_device::dump_config()
{
    virtio_pci_device::dump_config();

    _common_cfg->print("  common cfg:");
    _isr_cfg->print("  isr cfg:");
    _notify_cfg->print("  notify cfg:");
    _device_cfg->print("  device cfg:");
}

void virtio_modern_pci_device::kick_queue(int queue)
{
    auto offset = _notify_offset_multiplier * _queues_notify_offsets[queue];
    _notify_cfg->virtio_conf_writew(offset, queue);
}

void virtio_modern_pci_device::setup_queue(vring *queue)
{
    auto queue_index = queue->index();

    if (_dev->is_msix()) {
        // Setup queue_id:entry_id 1:1 correlation...
        _common_cfg->virtio_conf_writew(COMMON_CFG_OFFSET_OF(queue_msix_vector), queue_index);
        if (_common_cfg->virtio_conf_readw(COMMON_CFG_OFFSET_OF(queue_msix_vector)) != queue_index) {
            virtio_e("Setting MSIx entry for queue %d failed.", queue_index);
            return;
        }
    }

    _queues_notify_offsets[queue_index] =
            _common_cfg->virtio_conf_readw(COMMON_CFG_OFFSET_OF(queue_notify_off));

    _common_cfg->virtio_conf_writew(COMMON_CFG_OFFSET_OF(queue_size), queue->size());
    //
    // Pass addresses
    _common_cfg->virtio_conf_writel(COMMON_CFG_OFFSET_OF(queue_desc_lo), (u32)queue->get_desc_addr());
    _common_cfg->virtio_conf_writel(COMMON_CFG_OFFSET_OF(queue_desc_hi), (u32)(queue->get_desc_addr() >> 32));

    _common_cfg->virtio_conf_writel(COMMON_CFG_OFFSET_OF(queue_avail_lo), (u32)queue->get_avail_addr());
    _common_cfg->virtio_conf_writel(COMMON_CFG_OFFSET_OF(queue_avail_hi), (u32)(queue->get_avail_addr() >> 32));

    _common_cfg->virtio_conf_writel(COMMON_CFG_OFFSET_OF(queue_used_lo), (u32)queue->get_used_addr());
    _common_cfg->virtio_conf_writel(COMMON_CFG_OFFSET_OF(queue_used_hi), (u32)(queue->get_used_addr() >> 32));
}

void virtio_modern_pci_device::activate_queue(int queue)
{
    _common_cfg->virtio_conf_writew(COMMON_CFG_OFFSET_OF(queue_enable), 1);
}

void virtio_modern_pci_device::select_queue(int queue)
{
    _common_cfg->virtio_conf_writew(COMMON_CFG_OFFSET_OF(queue_select), queue);
}

u16 virtio_modern_pci_device::get_queue_size()
{
    return _common_cfg->virtio_conf_readw(COMMON_CFG_OFFSET_OF(queue_size));
}

u64 virtio_modern_pci_device::get_available_features()
{
    u64 features;

    _common_cfg->virtio_conf_writel(COMMON_CFG_OFFSET_OF(device_feature_select), 0);
    features = _common_cfg->virtio_conf_readl(COMMON_CFG_OFFSET_OF(device_feature));
    _common_cfg->virtio_conf_writel(COMMON_CFG_OFFSET_OF(device_feature_select), 1);
    features |= ((u64)_common_cfg->virtio_conf_readl(COMMON_CFG_OFFSET_OF(device_feature)) << 32);

    return features;
}

void virtio_modern_pci_device::set_enabled_features(u64 features)
{
    _common_cfg->virtio_conf_writel(COMMON_CFG_OFFSET_OF(driver_feature_select), 0);
    _common_cfg->virtio_conf_writel(COMMON_CFG_OFFSET_OF(driver_feature), (u32)features);
    _common_cfg->virtio_conf_writel(COMMON_CFG_OFFSET_OF(driver_feature_select), 1);
    _common_cfg->virtio_conf_writel(COMMON_CFG_OFFSET_OF(driver_feature), features >> 32);
}

u8 virtio_modern_pci_device::get_status()
{
    return _common_cfg->virtio_conf_readb(COMMON_CFG_OFFSET_OF(device_status));
}

void virtio_modern_pci_device::set_status(u8 status)
{
    _common_cfg->virtio_conf_writeb(COMMON_CFG_OFFSET_OF(device_status), status);
}

u8 virtio_modern_pci_device::read_config(u32 offset)
{
    return _device_cfg->virtio_conf_readb(offset);
}

u8 virtio_modern_pci_device::read_and_ack_isr()
{
    return _isr_cfg->virtio_conf_readb(0);
}

// Stores the address and length of the shared memory region identified by @id
// in @addr and @length respectively. Returns false and doesn't modify @addr and
// @length if no region with a matching id is found.
bool virtio_modern_pci_device::get_shm(u8 id, mmioaddr_t &addr, u64 &length)
{
    auto cap = std::find_if(_shm_cfgs.cbegin(), _shm_cfgs.cend(),
        [id, this] (const std::unique_ptr<virtio_capability>& cap) {
            u8 cap_id = _dev->pci_readb(cap->get_cfg_offset() +
                offsetof(virtio_pci_cap, id));
            return cap_id == id;
        });
    if (cap == _shm_cfgs.cend()) {
        return false;
    }

    auto bar = cap->get()->get_bar();
    if (!bar->is_mmio()) {
        return false;
    }
    addr = bar->get_mmio();
    length = bar->get_size();
    return true;
}

void virtio_modern_pci_device::find_vendor_capabilities(std::vector<std::pair<u8,u8>>& offsets_and_types)
{
    std::vector<u8> cap_offsets;
    if (_dev->find_capabilities(pci::function::PCI_CAP_VENDOR, cap_offsets)) {
        for (auto offset : cap_offsets) {
            u8 cfg_type = _dev->pci_readb(offset + offsetof(struct virtio_pci_cap, cfg_type));
            offsets_and_types.emplace_back(std::pair<u8,u8>(offset, cfg_type));
        }
    }
}

bool virtio_modern_pci_device::parse_pci_config()
{
    // Check ABI version
    u8 rev = _dev->get_revision_id();
    if (rev < VIRTIO_PCI_MODERN_ABI_VERSION) {
        virtio_e("Wrong virtio revision=%x", rev);
        return false;
    }

    // Check device ID
    u16 dev_id = _dev->get_device_id();
    if ((dev_id < VIRTIO_PCI_MODERN_ID_MIN) || (dev_id > VIRTIO_PCI_MODERN_ID_MAX)) {
        virtio_e("Wrong virtio dev id %x", dev_id);
        return false;
    }

    std::vector<std::pair<u8,u8>> offsets_and_types;
    find_vendor_capabilities(offsets_and_types);

    parse_virtio_capability(offsets_and_types, _common_cfg, VIRTIO_PCI_CAP_COMMON_CFG);
    parse_virtio_capability(offsets_and_types, _isr_cfg, VIRTIO_PCI_CAP_ISR_CFG);
    parse_virtio_capability(offsets_and_types, _notify_cfg, VIRTIO_PCI_CAP_NOTIFY_CFG);
    parse_virtio_capability(offsets_and_types, _device_cfg, VIRTIO_PCI_CAP_DEVICE_CFG);
    parse_virtio_capabilities(offsets_and_types, _shm_cfgs, VIRTIO_PCI_CAP_SHARED_MEMORY_CFG);

    if (_notify_cfg) {
        _notify_offset_multiplier =_dev->pci_readl(_notify_cfg->get_cfg_offset() +
                offsetof(virtio_pci_notify_cap, notify_offset_multiplier));
    }

    // The common, isr and notifications configurations are mandatory
    return _common_cfg && _isr_cfg && _notify_cfg;
}

pci::bar* virtio_modern_pci_device::map_capability_bar(u8 cap_offset, u8 &bar_no)
{
    u8 bar_index = _dev->pci_readb(cap_offset + offsetof(struct virtio_pci_cap, bar));
    bar_no = bar_index + 1;
    auto bar = _dev->get_bar(bar_no);
    if (bar && bar->is_mmio() && !bar->is_mapped()) {
        bar->map();
    }
    return bar;
}

// Parse a single virtio PCI capability, whose type must match @type and store
// it in @ptr.
void virtio_modern_pci_device::parse_virtio_capability(std::vector<std::pair<u8,u8>> &offsets_and_types,
        std::unique_ptr<virtio_capability> &ptr, u8 type)
{
    u8 cfg_offset = 0xFF;
    for (auto cfg_offset_and_type: offsets_and_types) {
        auto cfg_type = cfg_offset_and_type.second;
        if (cfg_type == type) {
            cfg_offset = cfg_offset_and_type.first;
            break;
        }
    }

    if (cfg_offset != 0xFF) {
        u8 bar_no;
        auto bar = map_capability_bar(cfg_offset, bar_no);

        u64 offset = _dev->pci_readl(cfg_offset + offsetof(struct virtio_pci_cap, offset));
        u64 length = _dev->pci_readl(cfg_offset + offsetof(struct virtio_pci_cap, length));

        ptr.reset(new virtio_modern_pci_device::virtio_capability(cfg_offset, bar, bar_no, offset, length));
    }
}

// Parse all virtio PCI capabilities whose types match @type and append them to
// @caps.
// From the spec: "The device MAY offer more than one structure of any type -
// this makes it possible for the device to expose multiple interfaces to
// drivers. The order of the capabilities in the capability list specifies the
// order of preference suggested by the device. A device may specify that this
// ordering mechanism be overridden by the use of the id field."
void virtio_modern_pci_device::parse_virtio_capabilities( std::vector<std::pair<u8,u8>> &offsets_and_types,
                                                          std::vector<std::unique_ptr<virtio_capability>>& caps, u8 type)
{
    for (auto cfg_offset_and_type: offsets_and_types) {
        auto cfg_type = cfg_offset_and_type.second;
        if (cfg_type != type) {
            continue;
        }

        auto cfg_offset = cfg_offset_and_type.first;
        u8 bar_no;
        auto bar = map_capability_bar(cfg_offset, bar_no);

        u64 offset = _dev->pci_readl(cfg_offset + offsetof(struct virtio_pci_cap, offset));
        u64 length = _dev->pci_readl(cfg_offset + offsetof(struct virtio_pci_cap, length));
        if (type == VIRTIO_PCI_CAP_SHARED_MEMORY_CFG) {
            // The shared memory region capability is defined by a struct
            // virtio_pci_cap64
            u32 offset_hi = _dev->pci_readl(cfg_offset + offsetof(virtio_pci_cap64, offset_hi));
            u32 length_hi = _dev->pci_readl(cfg_offset + offsetof(virtio_pci_cap64, length_hi));
            offset |= ((u64)offset_hi << 32);
            length |= ((u64)length_hi << 32);
        }

        caps.emplace_back(new virtio_modern_pci_device::virtio_capability(
            cfg_offset, bar, bar_no, offset, length));
    }
}

virtio_device* create_virtio_pci_device(pci::device *dev) {
    if (dev->get_device_id() >= VIRTIO_PCI_MODERN_ID_MIN && dev->get_device_id() <= VIRTIO_PCI_MODERN_ID_MAX)
        return new virtio_modern_pci_device(dev);
    else
        return new virtio_legacy_pci_device(dev);
}

}
