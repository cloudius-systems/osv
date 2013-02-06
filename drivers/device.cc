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
        _devices.insert(std::make_pair(dev->get_id(), dev));
        return (true);
    }

    hw_device* device_manager::get_device(hw_device_id id, unsigned idx)
    {
        auto ppp = _devices.equal_range(id);

        unsigned cnt=0;
        for (auto it=ppp.first; it!=ppp.second; ++it) {
            if (cnt == idx) {
                return ((*it).second);
            }

            cnt++;
        }

        return (nullptr);
    }

    unsigned device_manager::get_num_devices(hw_device_id id)
    {
        return _devices.count(id);
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
