Devices and Drivers in OSv
==========================

1. Devices

  1. The most abstract data type of a device is hw_device (drivers/device.hh). Each device (PCI and non PCI) must implement 3 basic functions by means of inheritance:
    
    i. `device_id get_id(void)    // device_id identifies a device by a vendor_id/device_id pair`
    
    ii. `void print(void)         // implements a debug print function`
    
    iii. `void reset(void)        // brings the device to it's initial state`

  2. hw_device is managed by a global device_manager, implemented as a singleton. device_manager implements these functions:
    
    i. `bool register_device(hw_device* dev);`
    
    ii. `hw_device* get_device(hw_device_id id);`
    
    iii. `void list_devices(void);`
    
    iv. `void for_each_device(std::function<void (hw_device*)> func);`
    
  3. Internally, device_manager saves the hw_device(s) into a dictionary, keyed by device_id, so it performs:
    
    ```c++
    _devices.insert(std::make_pair(dev->get_id(), dev);
    ```

  4. There are several types of devices, namely pci_function, pci_device, pci_bridge and virtio_device.

2. Devices with PCI
  1. pci_function inherits from hw_device, it implements generic access to the PCI configuration space indexed by BDF triplet (Bus, Device, Function).
  2. pci_function is the base class of pci_device and pci_bridge, the PCI subsystem (pci.hh, pci.cc) uses, allocates and initializes these classes.
  3. A virtual function called parse_pci_config() is used to query the PCI configuration space for resources and configuration. Here's the code from the PCI subsystem that initializes and registers a PCI device:
        
        ```c++
        // See pci_device_enumeration() for more details:
        pci_function * dev = nullptr;
        if (pci_function::is_bridge(bus, slot, func)) {
            dev = new pci_bridge(bus, slot, func);
        } else {
            if (pci_device::is_virtio_device(bus, slot, func)) {
                dev = new virtio::virtio_device(bus, slot, func);
            } else {
                dev = new pci_device(bus, slot, func);
            }
        } 
        bool parse_ok = dev->parse_pci_config();
        if (!parse_ok) {
            debug(fmt("Error: couldn't parse device config space %x:%x.%")
                % bus % slot % func);
            break;
        }
        if (!device_manager::instance()->register_device(dev)) {
            debug(fmt("Error: couldn't register device %x:%x.%")
                % bus % slot % func);
            delete (dev);
        }
        ```

  4. Only after calling parse_pci_config(), pci_function will return a valid device_id ready to be indexed by the device_manager. parse_pci_config may return false in case it doesn't find what it was looking for.

3. VirtIO Devices

  1. virtio_device inherits from pci_device, virtio_device's parse_pci_config is looking for BAR #1 (port i/o) and only if it had been found it returns true. virtio_device implements access functions to the virtio config space, and also manages the device virtqueues.
  
4. Drivers

  1. Drivers may live without a device, or manage multiple devices. So the hw_driver base class is decoupled from a hw_device. Classes that realize hw_driver's interface may hold a reference to a single device, multiple devices or no device at all. The hw_driver base class is defined like this:

        ```c++
        // Drivers are indexed by their names
        virtual const std::string get_name(void) = 0;
        // Probe for connected hw,
        // return true if hw is found (query device_manager)
        virtual bool hw_probe(void) = 0;
        // System wide events
        virtual bool load(void) = 0;
        virtual bool unload(void) = 0;
        virtual void dump_config() = 0;
        ```

  2. Drivers are managed by the driver_manager, a simple singleton that drivers should register with, like this (locader.cc: do_main_thread()) -
        
        ```c++
        hw::driver_manager* drvman = hw::driver_manager::instance();
        drvman->register_driver(new virtio::virtio_blk());
        drvman->register_driver(new virtio::virtio_net());
        drvman->load_all();
        ```

  3. A driver can register with the driver_manager ONLY IF it's hw_probe() function returns true. Drivers that don't require a device should simply return true. A driver that drives hw, should probe for it's existance by quering the device_manager for example. The load()/unload() functions are used by the system. For more details, see how VirtIO drivers are realizing this interface.

  4. VirtIO drivers have a base class of virtio_driver. It holds a reference to virtio_device* _dev; it's hw_probe() function is implemented like this:

        ```c++
        _dev = dynamic_cast<virtio_device *>(device_manager::instance()->
            get_device(hw_device_id(VIRTIO_VENDOR_ID, _device_id)));
        if (_dev == nullptr) {
            return (false);
        }
        return (true);
        ```
   
        _device_id is a local member that is being initialized by a child class. virtio_net is initialized like this:

        ```c++
        virtio_net::virtio_net()
            : virtio_driver(VIRTIO_NET_DEVICE_ID)
        {

        }
        const std::string virtio_net::get_name(void)
        { 
            return "virtio-net";
        }
        ```
