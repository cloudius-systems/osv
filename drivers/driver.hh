#ifndef DRIVER_H
#define DRIVER_H

#include "drivers/pci.hh"
#include "drivers/pci-device.hh"
#include <map>
#include <string>

using namespace pci;

namespace hw {

    class hw_driver {
    public:
        virtual ~hw_driver() {};

        // Drivers are indexed by their names
        virtual const std::string get_name(void) = 0;

        // Probe for connected hw,
        // return true if hw is found (query device_manager)
        virtual bool hw_probe(void) = 0;

        // System wide events
        virtual bool load(void) = 0;
        virtual bool unload(void) = 0;
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

        bool register_driver(hw_driver* drv);
        void load_all(void);
        void unload_all(void);
        void list_drivers(void);

    private:
        static driver_manager* _instance;
        std::map<std::string, hw_driver*> _drivers;
    };
}


#endif
