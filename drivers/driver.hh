#ifndef DRIVER_H
#define DRIVER_H

#include "arch/x64/processor.hh"
#include "drivers/pci.hh"
#include "drivers/device.hh"
#include <ostream>
#include <unordered_map>

using namespace processor;
using namespace pci;

class Driver {
public:
    Driver(u16 vid, u16 id) :_id(id), _vid(vid), _present(false), _bus(0), _slot(0), _func(0)\
           {for (int i=0;i<6;i++) _bars[i] = nullptr;};
    virtual ~Driver() {for (int i=0;i<6;i++) if (_bars[i]) delete _bars[i];}

    bool isPresent();
    void setPresent(u8 bus, u8 slot, u8 func);
    u8 getBus();
    u8 getSlot();
    u8 getFunc();

    u16 getStatus();
    void setStatus(u16 s);
    bool getBusMaster();
    void setBusMaster(bool m);
    virtual void dumpConfig() const;
    virtual bool Init(Device *d);

    bool parse_pci_config(void);

    friend std::ostream& operator <<(std::ostream& out, const Driver &d);
    struct equal {
      bool operator()(const Driver* d1, const Driver* d2) const
      {
        return (d1->_id == d2->_id && d1->_vid == d2->_vid);
      }
    };

    struct hash : std::unary_function< const Driver*, std::size_t> {
        std::size_t operator() ( const Driver* const key ) const {
           return (size_t)(key->_id + key->_vid);
        }
    };

    class InitException {
        const char* what() const { return "uninitialized driver"; }
    };


    u8 getRevision();
    u16 getSubsysId();
    u16 getSubsysVid();

    void initBars();

protected:

    bool pciEnable();
    bool allocateBARs();
    virtual bool earlyInitChecks();

    // Parsing of extra capabilities
    virtual bool parse_pci_capabilities(void);
    virtual bool parse_pci_msix(void);

    // Access to PCI address space
    virtual u8 pci_readb(u8 offset);
    virtual u16 pci_readw(u8 offset);
    virtual u32 pci_readl(u8 offset);
    virtual void pci_writeb(u8 offset, u8 val);
    virtual void pci_writew(u8 offset, u16 val);
    virtual void pci_writel(u8 offset, u32 val);

    u16 _id;
    u16 _vid;
    bool _present;
    u8  _bus, _slot, _func;
    Bar* _bars[6];

    // MSI-X
    bool _have_msix;

};

#endif
