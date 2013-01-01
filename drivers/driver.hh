#ifndef DRIVER_H
#define DRIVER_H

#include "arch/x64/processor.hh"
#include "drivers/pci.hh"
#include "drivers/device.hh"
#include <ostream>
#include <unordered_map>

using namespace processor;

class Driver {
public:
    Driver(u16 vid, u16 id) :_id(id), _vid(vid) {};

    bool isPresent();
    void setPresent(u8 bus, u8 slot, u8 func);
    u16 getStatus();
    void setStatus(u16 s);
    bool getBusMaster();
    void setBusMaster(bool m);
    virtual void dumpConfig() const;
    virtual bool Init(Device *d);

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


protected:

    bool pciEnable();
    bool allocateBARs();
    virtual bool earlyInitChecks();

    u16 _id;
    u16 _vid;
    bool _present;
    u8  _bus, _slot, _func;
};

#endif
