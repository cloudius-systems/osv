/*
 * Copyright (C) 2013 Cloudius Systems, Ltd.
 *
 * This work is open source software, licensed under the terms of the
 * BSD license as described in the LICENSE file in the top-level directory.
 */

#ifndef DEVICE_HH
#define DEVICE_HH

#include <functional>
#include <map>

#include <osv/types.h>

namespace hw {

    // generic id for device (pci or non-pci)
    class hw_device_id {
    public:
        hw_device_id(u16 vendor_id, u16 device_id)
            : _vendor_id(vendor_id), _device_id(device_id) {}

        u32 make32(void) const {
            return ((u32)_vendor_id<<16 | _device_id);
        }

        bool operator<(const hw_device_id& other) const {
            return (this->make32() < other.make32());
        }

        bool operator==(const hw_device_id& other) const {
            return _vendor_id == other._vendor_id
                    && _device_id == other._device_id;
        }

    private:
        u16 _vendor_id;
        u16 _device_id;
    };


    class hw_device {
    public:
        virtual ~hw_device() {};

        // Unique vendor/device ids
        virtual hw_device_id get_id(void) = 0;

        // Debug print of device
        virtual void print(void) = 0;

        // After calling reset the device should be in init state
        virtual void reset(void) = 0;
    };


    class device_manager {
    public:

        device_manager();
        virtual ~device_manager();

        static device_manager* instance() {
            if (_instance == nullptr) {
                _instance = new device_manager();
            }
            return (_instance);
        }

        // Adds to dictionary
        bool register_device(hw_device* dev);

        // Retrieves from dictionary
        hw_device* get_device(hw_device_id id, unsigned idx=0);
        unsigned get_num_devices(hw_device_id id);

        // System wide operations
        void list_devices(void);
        void for_each_device(std::function<void (hw_device*)> func);

    private:
        static device_manager* _instance;
        std::multimap<hw_device_id, hw_device*> _devices;
    };

}

#endif // DEVICE_HH
