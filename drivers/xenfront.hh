#ifndef XENFRONT_DRIVER_H
#define XENFRONT_DRIVER_H

#include "driver.hh"
#include "drivers/pci.hh"
#include "drivers/driver.hh"
#include "drivers/pci-function.hh"
#include "drivers/pci-device.hh"
#include "interrupt.hh"
#include <osv/device.h>
#include <bsd/porting/bus.h>
#include <xen/interface/io/xenbus.h>

#define XEN_VENDOR_ID 0x5853

struct xenbus_device_ivars;

template <typename T>
inline T *bsd_to_dev(struct device *bd)
{
    T *base = NULL;
    return reinterpret_cast<T *>((unsigned long)bd - (unsigned long)&base->_bsd_dev);
}

namespace xenfront {

typedef void (*probe)(device_t dev);
typedef int  (*attach)(device_t dev);
typedef void (*backend_changed)(device_t dev, XenbusState backend_state);

class xenbus;

class xenfront_driver {

public:
    explicit xenfront_driver();
    virtual ~xenfront_driver();
    struct device* device() { return &_bsd_dev; }
    static xenfront_driver *from_device(struct device *dev) { return bsd_to_dev<xenfront_driver>(dev); }

    virtual const std::string get_name(void) { return _driver_name; }
    const std::string get_type(void) { return _type; }
    const std::string get_node_path(void) { return _node_path; }
    const int get_otherend_id(void) { return _otherend; }
    const std::string get_otherend_path(void) { return _otherend_path; };
    void set_ivars(struct xenbus_device_ivars *ivars);

    virtual void localend_changed(std::string local) { };
    virtual void otherend_changed(XenbusState state);
    virtual int attach();
    virtual void probe();
    struct device _bsd_dev;
protected:
    xenbus *_bus;
    std::string _driver_name;
    std::string _node_path;
    std::string _otherend_path;
    std::string _type;
    int _otherend;
    unsigned int _irq;
    int _irq_type;

    xenfront::probe _probe;
    xenfront::attach _attach;
    xenfront::backend_changed _backend_changed;
};
}

#endif

