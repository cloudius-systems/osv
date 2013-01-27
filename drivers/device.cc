#include <functional>
#include <map>

#include "debug.hh"
#include "device.hh"

namespace hw {

    device_manager* device_manager::_instance = nullptr;

    device_manager::device_manager()
    {

    }

    device_manager::~device_manager()
    {
        for (auto it=_devices.begin(); it != _devices.end(); it++) {
            delete (it->second);
        }
    }

    bool device_manager::register_device(hw_device* dev)
    {
        if (get_device(dev->get_id()) != nullptr) {
            return (false);
        }

        _devices.insert(std::make_pair(dev->get_id(), dev));
        return (true);
    }

    hw_device* device_manager::get_device(hw_device_id id)
    {
        auto it = _devices.find(id);
        if (it == _devices.end()) {
            return (nullptr);
        }

        return (it->second);
    }

    void device_manager::list_devices(void)
    {
        debug("<list_devices>");
        for_each_device([](hw_device* dev) { dev->print(); });
        debug("</list_devices>");
    }

    void device_manager::for_each_device(std::function<void (hw_device*)> func)
    {
        for (auto it=_devices.begin(); it != _devices.end(); it++) {
            func(it->second);
        }
    }

}
