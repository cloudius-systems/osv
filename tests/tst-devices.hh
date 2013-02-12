#ifndef __TST_DEVICES__
#define __TST_DEVICES__

#include "tst-hub.hh"
#include "drivers/pci.hh"
#include "debug.hh"

class test_devices : public unit_tests::vtest {

public:

    void run()
    {
            // Comment it out to reduce output
            //pci::pci_devices_print();

            // List all devices
            hw::device_manager::instance()->list_devices();

            debug("Devices test succeeded");
    }
};

#endif
