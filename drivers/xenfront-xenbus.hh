#ifndef XENFRONT_BUS_DRIVER_H
#define XENFRONT_BUS_DRIVER_H

#include "drivers/xenfront.hh"
#include "drivers/pci-device.hh"
#include <osv/device.h>
#include <bsd/porting/bus.h>

namespace xenfront {

    class xenbus : public hw_driver {
    public:

        explicit xenbus(pci::device& dev);
        virtual ~xenbus();
        static hw_driver* probe(hw_device* dev);
        pci::device& pci_device() { return _dev; }

        bool parse_pci_config(void);
        virtual void dump_config(void);

        virtual const std::string get_name(void) { return _driver_name; }
        virtual const std::string get_node_path(void) { return _node_path; }

        struct device* device() { return &_bsd_dev; }
        std::vector<xenfront_driver *>& children(void) { return _children; }
        static xenbus *from_device(struct device *dev) { return bsd_to_dev<xenbus>(dev); }
        struct device _bsd_dev;
    private:
        pci::device& _dev;
        interrupt_manager _intx;
        pci::bar *_bar1;

        std::vector<xenfront_driver *> _children;
        std::string _driver_name;
        std::string _node_path;
        int _evtchn;
        int _domid;
    };
}

#endif

