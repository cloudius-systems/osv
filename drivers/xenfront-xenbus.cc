/*
 * Copyright (C) 2013 Cloudius Systems, Ltd.
 *
 * This work is open source software, licensed under the terms of the
 * BSD license as described in the LICENSE file in the top-level directory.
 */

#include "clock.hh"
#include "msr.hh"
#include <osv/types.h>
#include "mmu.hh"
#include "string.h"
#include "cpuid.hh"
#include "barrier.hh"
#include "debug.hh"
#include "xen.hh"
#include "processor.hh"
#include "xenfront.hh"
#include "xenfront-xenbus.hh"
#include "exceptions.hh"
#include <osv/device.h>
#include <bsd/porting/bus.h>
#include <xen/xenstore/xenstorevar.h>
#include <xen/xenbus/xenbusb.h>

extern "C" {

    int xs_attach(struct device *);

    int xenpci_irq_init(device_t device, struct xenpci_softc *scp);
    void evtchn_init(void *arg);

    int xenbusb_front_probe(device_t dev);
    int xenbusb_front_attach(device_t dev);
    int xenbusb_add_device(device_t dev, const char *type, const char *id);
};


namespace xenfront {

xenbus::xenbus(pci::device& pci_dev)
    : hw_driver()
    , _dev(pci_dev)
{
    static int initialized;
    int irqno = pci_dev.get_interrupt_line();

    if (!irqno)
        return;

    if (initialized++)
        return;

    parse_pci_config();

    _dev.set_bus_master(true);
    _driver_name = std::string("xenfront-xenbus");

    // From here on, all the OSV details are sorted, and we start the Xen
    // bringup
    evtchn_init(NULL);

    if (features().xen_vector_callback) {
        xen::xen_set_callback();
    } else {
        _pgsi.reset(xen::xen_set_callback(irqno));
    }

    xs_attach(&_xenstore_device);

    xs_scanf(XST_NIL, "domid", "", NULL, "%d", &_domid);
    _node_path = osv::sprintf("/local/domain/%d", _domid); 

    xenbusb_softc *xenbusb_scp = new xenbusb_softc;
    _bsd_dev.softc = xenbusb_scp;

    xenbusb_front_probe(&_bsd_dev);
    xenbusb_front_attach(&_bsd_dev);

    // The xenbus bringup is an asynchronous protocol. Therefore, if we just
    // return right away from here, the devices won't be ready by the time we
    // reach mount_usr(), or any of the other network functions. It is saner
    // to wait for them to be connected (or failed) before we continue.
    wait_for_devices();
}

void xenbus::wait_for_devices()
{
    WITH_LOCK(_children_mutex) {
        while (!_pending_children.empty() || _children.empty()) {
            condvar_wait(&_pending_devices, &_children_mutex, 1000_ms);
        }
        for (auto device : _pending_children) {
            debug("Device %s bringup failed\n", device->get_name());
        }
    }
}

void xenbus::remove_pending(xenfront_driver *dev)
{
    WITH_LOCK(_children_mutex) {
        _pending_children.remove(dev); 
        if (_pending_children.empty() && !_children.empty()) {
            condvar_wake_all(&_pending_devices);
        }
    }
}

void xenbus::add_pending(xenfront_driver *dev)
{
    WITH_LOCK(_children_mutex) {
        _children.push_back(dev);
        _pending_children.push_back(dev); 
    }
}

void xenbus::for_each_child(std::function<void(xenfront_driver *d)> func)
{
    WITH_LOCK(_children_mutex) {
        for (auto device : _children) {
            func(device);
        }
    }
}

hw_driver* xenbus::probe(hw_device* dev)
{
    if (!processor::features().xen_pci) {
        return nullptr;
    }

    if (auto pci_dev = dynamic_cast<pci::device*>(dev)) {
        // dev id is the same for all xen devices?
        if (pci_dev->get_subsystem_vid() == XEN_VENDOR_ID) {
            return new xenbus(*pci_dev);
        }
    }
    return nullptr;
}

void xenbus::dump_config()
{
    _dev.dump_config();
}

bool xenbus::parse_pci_config(void)
{
    return _dev.parse_pci_config();
}
};

#define bsd_to_xenfront(_x) xenfront::xenfront_driver::from_device(_x)
#define bsd_to_xenbus(_x) xenfront::xenbus::from_device(_x)

const char *xenbus_get_otherend_path(device_t _dev)
{
    return bsd_to_xenfront(_dev)->get_otherend_path().c_str();
}

const char *xenbus_get_type(device_t _dev)
{
    return bsd_to_xenfront(_dev)->get_type().c_str();
}

int XENBUSB_GET_OTHEREND_NODE(device_t dev, struct xenbus_device_ivars *ivars)
{
    u_int len;
    void *result;
    int error;

    error = xs_read(XST_NIL, ivars->xd_node, "backend", &len, &result);
    if (!error) {
        // result freed by xenbusb.c, at xenbusb_free_child_ivars()
        ivars->xd_otherend_path_len = len;
        ivars->xd_otherend_path = static_cast<char *>(result);
    }
    return error;
}

void XENBUSB_ENUMERATE_TYPE(device_t _bus, const char *type)
{
    xenfront::xenbus *bus = bsd_to_xenbus(_bus);

    if (!strcmp(type, "vbd") || !strcmp(type, "vif")) {
        u_int dev_count;
        const char **devices;

        std::string node = osv::sprintf("device/%d", type);

        if (xs_directory(XST_NIL, bus->get_node_path().c_str(), node.c_str(), &dev_count, &devices)) {
            return;
        }

        for (u_int i = 0; i < dev_count; i++) {                             
            xenbusb_add_device(_bus, type, devices[i]);
        } 
        // devices[i] does not to be freed because only devices comes from
        // malloc'ed memory. This is the same pattern taken at
        // xenbusb_enumerate_bus, xenbusb.c
        free(devices);
    }
}

void xenbus_set_state(device_t _dev, XenbusState state)
{
    xenfront::xenfront_driver *dev = bsd_to_xenfront(_dev);
    xs_write(XST_NIL, dev->get_node_path().c_str(), "state", std::to_string(state).c_str());

    if ((state == XenbusStateConnected) || (state == XenbusStateClosed)) {
        dev->finished();
    }
}

XenbusState xenbus_get_state(device_t _dev)
{
    xenfront::xenfront_driver *dev = bsd_to_xenfront(_dev);

    int state;
    xs_scanf(XST_NIL, dev->get_node_path().c_str(), "state", NULL, "%d", &state);

    // Should really be XenbusStateLast or so
    assert(state <= XenbusStateReconfigured);
    return XenbusState(state);
}


void XENBUSB_OTHEREND_CHANGED(device_t _bus, device_t _dev, XenbusState newstate)
{
    bsd_to_xenfront(_dev)->otherend_changed(newstate);
}

void XENBUSB_LOCALEND_CHANGED(device_t _bus, device_t _dev, const char *path)
{
    bsd_to_xenfront(_dev)->localend_changed(path);
}

device_t device_add_child(device_t _bus, const char *devpath, int unit)
{
    xenfront::xenbus *bus = bsd_to_xenbus(_bus);
    xenfront::xenfront_driver *child;

    child = new xenfront::xenfront_driver(bus);

    bus->add_pending(child);
    // The bare minimum initialization we need.
    child->_bsd_dev.state = DS_NOTPRESENT;
    return &child->_bsd_dev;
}

int device_get_children(device_t _bus, device_t **devlistp, int *devcountp)
{
    xenfront::xenbus *bus = bsd_to_xenbus(_bus);
    device_t *list = static_cast<device_t *>(malloc(bus->num_children() * sizeof(device_t)));

    int i = 0;
    bus->for_each_child([=, &i](xenfront::xenfront_driver *d) {
            list[i++] = &d->_bsd_dev;
    });

    *devcountp = i;
    *devlistp = list;

    return 0;
}

const char *xenbus_get_node(device_t _dev)
{
    return bsd_to_xenfront(_dev)->get_node_path().c_str();
}

int xenbus_get_otherend_id(device_t dev)
{
    return bsd_to_xenfront(dev)->get_otherend_id();
}

void
device_set_ivars(device_t _dev, void *_ivars)
{
    struct xenbus_device_ivars *ivars;
    _dev->ivars = _ivars;
    ivars = static_cast<struct xenbus_device_ivars *>(_ivars);

    bsd_to_xenfront(_dev)->set_ivars(ivars);
}

int
device_probe_and_attach(device_t _dev)
{
    bsd_to_xenfront(_dev)->probe();
    _dev->state = DS_ATTACHING;
    int ret = bsd_to_xenfront(_dev)->attach();
    if (!ret)
        _dev->state = DS_ATTACHED;
    return ret;
}
