#include "drivers/driver.hh"
#include "drivers/pci.hh"
#include "debug.hh"

#include "driver.hh"

using namespace pci;

namespace hw {

    driver_manager* driver_manager::_instance = nullptr;

    driver_manager::driver_manager()
    {

    }

    driver_manager::~driver_manager()
    {
        unload_all();

        for (auto it=_drivers.begin(); it != _drivers.end(); it++) {
            delete(it->second);
        }

        _drivers.clear();
    }

    bool driver_manager::register_driver(hw_driver* drv)
    {
        debug(fmt("Probing '%1%'... ") % drv->get_name(), false);

        if (drv->hw_probe()) {
            _drivers.insert(std::make_pair(drv->get_name(), drv));
            debug("OK!");
            return (true);
        } else {
            debug("NOK");
            return (false);
        }
    }

    void driver_manager::load_all(void)
    {
        for (auto it=_drivers.begin(); it != _drivers.end(); it++) {
            hw_driver * drv = it->second;
            debug(fmt("Loading Driver: '%1%'...") % drv->get_name());
            drv->load();
        }
    }

    void driver_manager::unload_all(void)
    {
        for (auto it=_drivers.begin(); it != _drivers.end(); it++) {
            hw_driver * drv = it->second;
            drv->unload();
        }
    }

    void driver_manager::list_drivers(void)
    {
        debug("<list_drivers>");
        for (auto it=_drivers.begin(); it != _drivers.end(); it++) {
            hw_driver * drv = it->second;
            drv->dump_config();
        }
        debug("</list_drivers>");
    }
}
