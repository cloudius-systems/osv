/*
 * Copyright (C) 2013 Cloudius Systems, Ltd.
 *
 * This work is open source software, licensed under the terms of the
 * BSD license as described in the LICENSE file in the top-level directory.
 */

#include <string.h>

#include "drivers/xenfront.hh"
#include "drivers/xenfront-xenbus.hh"
#include <osv/debug.h>
#include <bsd/porting/bus.h>
#include <xen/xenstore/xenstorevar.h>
#include <xen/xenbus/xenbusb.h>
#include <osv/bio.h>
#include "sys/xen/gnttab.h"
#include "sys/dev/xen/blkfront/block.h"
#include <osv/string_utils.hh>
#include <osv/kernel_config_networking_stack.h>

extern driver_t netfront_driver;
extern driver_t blkfront_driver;

namespace xenfront {

void xenfront_driver::otherend_changed(XenbusState state)
{
    if (_backend_changed)
        _backend_changed(&_bsd_dev, state);
}

void xenfront_driver::probe()
{
    if (_probe)
        _probe(&_bsd_dev);
}

int xenfront_driver::detach()
{
    if (_detach) {
        return _detach(&_bsd_dev);
    }
    return 0;
}

int xenfront_driver::attach()
{
    if (_attach)
        return _attach(&_bsd_dev);
    return 0;
}

void xenfront_driver::set_ivars(struct xenbus_device_ivars *ivars)
{
    driver_t *table;

    _otherend_path = std::string(ivars->xd_otherend_path);
    _otherend_id = ivars->xd_otherend_id;
    _node_path = std::string(ivars->xd_node);
    _type = std::string(ivars->xd_type);

#if CONF_networking_stack
    if (!strcmp(ivars->xd_type, "vif")) {
        table = &netfront_driver;
        _irq_type = INTR_TYPE_NET;
        _driver_name = "vif";

        std::vector<std::string> node_info;
        osv::split(node_info, _node_path, "/");
        assert(node_info.size() == 3);

        // Very unfrequent, so don't care about how expensive and full of barriers this is
        _bsd_dev.unit = stoi(node_info[2]);

        _bsd_dev.softc = malloc(table->size);
        // Simpler and we don't expect driver loading to fail anyway
        assert(_bsd_dev.softc);
        memset(_bsd_dev.softc, 0, table->size);
    } else
#endif
    if (!strcmp(ivars->xd_type, "vbd")) {
        table = &blkfront_driver;
        _irq_type = INTR_TYPE_BIO;
        _driver_name = "vblk";
        _bsd_dev.softc = reinterpret_cast<void *>(new bf_softc);
    } else
        return;

    device_method_t *dm = table->methods;
    for (auto i = 0; dm[i].id; i++) {
        if (dm[i].id == bus_device_probe)
            _probe = reinterpret_cast<xenfront::probe>(dm[i].func);
        if (dm[i].id == bus_device_attach)
            _attach = reinterpret_cast<xenfront::attach>(dm[i].func);
        if (dm[i].id == bus_device_detach)
            _detach = reinterpret_cast<xenfront::detach>(dm[i].func);
        if (dm[i].id == bus_xenbus_otherend_changed)
            _backend_changed = reinterpret_cast<xenfront::backend_changed>(dm[i].func);
    }
}

void xenfront_driver::finished()
{
    if (_irq_type == INTR_TYPE_BIO)
        read_partition_table(&this->_bsd_dev);
    _bus->remove_pending(this);
}

xenfront_driver::xenfront_driver(xenbus *bus)
    : _bus(bus)
{
}
}
