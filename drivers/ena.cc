/*
 * Copyright (C) 2023 Waldemar Kozaczuk
 *
 * This work is open source software, licensed under the terms of the
 * BSD license as described in the LICENSE file in the top-level directory.
 */

#include <sys/cdefs.h>

#include "drivers/ena.hh"
#include "drivers/pci-device.hh"

#include <osv/aligned_new.hh>

#include <bsd/sys/net/ethernet.h>

extern bool opt_maxnic;
extern int maxnic;

namespace aws {

#define ena_tag "ena"
#define ena_d(...)   tprintf_d(ena_tag, __VA_ARGS__)
#define ena_i(...)   tprintf_i(ena_tag, __VA_ARGS__)
#define ena_w(...)   tprintf_w(ena_tag, __VA_ARGS__)
#define ena_e(...)   tprintf_e(ena_tag, __VA_ARGS__)

/* TODO - figure out how and if needed to integrate it - ENA code has it own logic to track statistics
static void if_getinfo(struct ifnet* ifp, struct if_data* out_data)
{
    ena* _ena = (ena*)ifp->if_softc;

    // First - take the ifnet data
    memcpy(out_data, &ifp->if_data, sizeof(*out_data));

    // then fill the internal statistics we've gathered
    _ena->fill_stats(out_data);
}*/

void ena::fill_stats(struct if_data* out_data) const
{
    assert(!out_data->ifi_oerrors && !out_data->ifi_obytes && !out_data->ifi_opackets);
    /* TODO - figure out how and if needed to integrate it - ENA code has it own logic to track statistics
    out_data->ifi_ipackets += _rxq[0].stats.rx_packets;
    out_data->ifi_ibytes   += _rxq[0].stats.rx_bytes;
    out_data->ifi_iqdrops  += _rxq[0].stats.rx_drops;
    out_data->ifi_ierrors  += _rxq[0].stats.rx_csum_err;
    out_data->ifi_opackets += _txq[0].stats.tx_packets;
    out_data->ifi_obytes   += _txq[0].stats.tx_bytes;
    out_data->ifi_oerrors  += _txq[0].stats.tx_err + _txq[0].stats.tx_drops;

    out_data->ifi_iwakeup_stats = _rxq[0].stats.rx_wakeup_stats;
    out_data->ifi_owakeup_stats = _txq[0].stats.tx_wakeup_stats;*/
}

ena::ena(pci::device &dev)
    : _dev(dev)
{
    _adapter = nullptr;
    auto ret = ena_attach(&_dev, &_adapter);
    if (ret || !_adapter) {
       throw std::runtime_error("Failed to attach ena device");
    }

    //TODO _ifn->if_getinfo = if_getinfo;
}

ena::~ena()
{
    ena_detach(_adapter);
    _adapter = nullptr;
}

void ena::dump_config(void)
{
    u8 B, D, F;
    _dev.get_bdf(B, D, F);

    _dev.dump_config();
    ena_d("%s [%x:%x.%x] vid:id= %x:%x", get_name().c_str(),
        (u16)B, (u16)D, (u16)F,
        _dev.get_vendor_id(),
        _dev.get_device_id());
}

hw_driver* ena::probe(hw_device* dev)
{
    try {
        if (auto pci_dev = dynamic_cast<pci::device*>(dev)) {
            pci_dev->dump_config();
            if (ena_probe(pci_dev)) {
                if (opt_maxnic && maxnic-- <= 0) {
                    return nullptr;
                } else {
                    return aligned_new<ena>(*pci_dev);
                }
            }
        }
    } catch (std::exception& e) {
        ena_e("Exception on device construction: %s", e.what());
    }
    return nullptr;
}

}
