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
#include <sstream>
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
    , _intx(&pci_dev)
    , _bar1(nullptr)
{
    static int initialized;
    int irqno = pci_dev.get_interrupt_line();

    if (!irqno)
        return;

    if (initialized++)
        return;

    parse_pci_config();

    _dev.set_bus_master(true);

    std::stringstream ss;
    ss << "xenfront-xenbus";
    _driver_name = ss.str();

    evtchn_init(NULL);
    xen::xen_set_callback();

    // scp for xs is a static variable at the xenstore
    struct device *xs_dev = new struct device;
    xs_attach(xs_dev);

    xs_scanf(XST_NIL, "domid", "", NULL, "%d", &_domid);

    std::stringstream node;
    node << "/local/domain/";
    node << _domid;
    _node_path = node.str();

    xenbusb_softc *xenbusb_scp = new xenbusb_softc;
    _bsd_dev.softc = xenbusb_scp;

    xenbusb_front_probe(&_bsd_dev);
    xenbusb_front_attach(&_bsd_dev);
}

xenbus::~xenbus()
{
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

#define xenfront_tag "xenfront"
#define xenbus_d(...)   tprintf_d(xenfront_tag, __VA_ARGS__)

void xenbus::dump_config()
{
    u8 B, D, F;
    _dev.get_bdf(B, D, F);

    _dev.dump_config();
    xenbus_d("%s [%x:%x.%x] vid:id= %x:%x", get_name().c_str(),
        (u16)B, (u16)D, (u16)F,
        _dev.get_vendor_id(),
        _dev.get_device_id());
}

bool xenbus::parse_pci_config(void)
{
    if (!_dev.parse_pci_config()) {
        return (false);
    }

    // Test whether bar1 is present
    _bar1 = _dev.get_bar(1);
    if (_bar1 == nullptr) {
        return (false);
    }
    return (true);
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

        std::string node = "device/";
        node += type;

        if (xs_directory(XST_NIL, bus->get_node_path().c_str(), node.c_str(), &dev_count, &devices)) {
            return;
        }

        for (u_int i = 0; i < dev_count; i++) {                             
            xenbusb_add_device(_bus, type, devices[i]);
        } 
        free(devices);
    }
}

void xenbus_set_state(device_t _dev, XenbusState state)
{
    xenfront::xenfront_driver *dev = bsd_to_xenfront(_dev);

    xs_write(XST_NIL, dev->get_node_path().c_str(), "state", std::to_string(state).c_str());
}

XenbusState xenbus_get_state(device_t _dev)
{
    XenbusState state;
    xenfront::xenfront_driver *dev = bsd_to_xenfront(_dev);

    xs_scanf(XST_NIL, dev->get_node_path().c_str(), "state", NULL, "%d", &state);

    // Should really be XenbusStateLast or so
    assert(state <= XenbusStateReconfigured);
    return state;
}


void XENBUSB_OTHEREND_CHANGED(device_t _bus, device_t _dev, XenbusState newstate)
{
    bsd_to_xenfront(_dev)->otherend_changed(newstate);
}

void XENBUSB_LOCALEND_CHANGED(device_t _bus, device_t _dev, const char *path)
{
    bsd_to_xenfront(_dev)->localend_changed(std::string(path));
}

device_t device_add_child(device_t _bus, const char *devpath, int unit)
{
    xenfront::xenbus *bus = bsd_to_xenbus(_bus);
    auto children = &bus->children();
    device_t bsd_dev;
    xenfront::xenfront_driver *child;

    child = new xenfront::xenfront_driver();

    // This is a bit of a hack, maybe we could initialize the rest of the device from set_ivars...
    children->push_back(child); 

    bsd_dev = child->device();
    // The bare minimum initialization we need.
    bsd_dev->state = DS_NOTPRESENT;
    return bsd_dev;
}

int device_get_children(device_t _bus, device_t **devlistp, int *devcountp)
{
    xenfront::xenbus *bus = bsd_to_xenbus(_bus);
    auto bus_children = bus->children();
    int devices = bus_children.size();
    device_t *list;

    list = static_cast<device_t *>(malloc(devices * sizeof(device_t)));
    if (!list)
        return -ENOMEM;

    int i = 0;
    for (auto device : bus_children) {
        list[i++] = device->device();
    }
    *devcountp = devices;
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
