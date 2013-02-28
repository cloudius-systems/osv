#ifndef DRIVER_H
#define DRIVER_H

#include "drivers/pci.hh"
#include "drivers/pci-device.hh"
#include <vector>
#include <string>

namespace hw {

    class hw_driver {
    public:
        virtual ~hw_driver() {};

        // Drivers are indexed by their names
        virtual const std::string get_name(void) = 0;

        // virtual bool sleep(void) = 0;
        // virtual bool wake(void) = 0;
        // ...

        virtual void dump_config() = 0;
    };

    class driver_manager {
    public:
        driver_manager();
        virtual ~driver_manager();

        static driver_manager* instance() {
            if (_instance == nullptr) {
                _instance = new driver_manager();
            }

            return (_instance);
        }

        void register_driver(std::function<hw_driver* (hw_device*)> probe);
        void load_all(void);
        void unload_all(void);
        void list_drivers(void);

    private:
        static driver_manager* _instance;
        std::vector<std::function<hw_driver* (hw_device*)>> _probes;
        std::vector<hw_driver*> _drivers;
    };
}


#endif
