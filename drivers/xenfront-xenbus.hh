/*
 * Copyright (C) 2013 Cloudius Systems, Ltd.
 *
 * This work is open source software, licensed under the terms of the
 * BSD license as described in the LICENSE file in the top-level directory.
 */

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
        static hw_driver* probe(hw_device* dev);
        pci::device& pci_device() { return _dev; }

        bool parse_pci_config(void);
        void dump_config(void);

        const std::string get_name(void) { return _driver_name; }
        const std::string &get_node_path(void) { return _node_path; }

        int num_children(void) { return _children.size(); }
        void for_each_child(std::function<void(xenfront_driver *dev)> func);

        void remove_pending(xenfront_driver *dev);
        void add_pending(xenfront_driver *dev);

        static xenbus *from_device(struct device *dev) { return bsd_to_dev<xenbus>(dev); }
        struct device _bsd_dev;
    private:
        void wait_for_devices(void);
        pci::device& _dev;
        std::unique_ptr<gsi_level_interrupt> _pgsi;
        struct device _xenstore_device;

        std::vector<xenfront_driver *> _children;
        mutex_t _children_mutex;

        sched::thread *_main_thread;
        std::string _driver_name;
        std::string _node_path;
        int _evtchn;
        int _domid;

        condvar_t _pending_devices;
        std::list<xenfront_driver *> _pending_children;
    };
}

#endif

