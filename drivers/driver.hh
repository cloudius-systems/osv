/* This header is BSD licensed so anyone can use the definitions to implement
 * compatible drivers/servers.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions
 * are met:
 * 1. Redistributions of source code must retain the above copyright
 *    notice, this list of conditions and the following disclaimer.
 * 2. Redistributions in binary form must reproduce the above copyright
 *    notice, this list of conditions and the following disclaimer in the
 *    documentation and/or other materials provided with the distribution.
 * 3. Neither the name of IBM nor the names of its contributors
 *    may be used to endorse or promote products derived from this software
 *    without specific prior written permission.
 * THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS ``AS IS'' AND
 * ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE
 * IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE
 * ARE DISCLAIMED.  IN NO EVENT SHALL IBM OR CONTRIBUTORS BE LIABLE
 * FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL
 * DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS
 * OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION)
 * HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT
 * LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY
 * OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF
 * SUCH DAMAGE. */

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
    virtual ~Driver();

    bool isPresent();
    void setPresent(u8 bus, u8 slot, u8 func);
    u8 getBus();
    u8 getSlot();
    u8 getFunc();

    u16 getStatus();
    void setStatus(u16 s);
    u16 get_command(void);
    void set_command(u16 c);
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

    // Enable/Disable intx assertions
    bool is_intx_enabled(void);
    // Enable intx assertion
    // intx assertions should be disabled in order to use MSI-x
    void enable_intx(void);
    void disable_intx(void);

    // Parsing of extra capabilities
    virtual bool parse_pci_capabilities(void);
    virtual bool parse_pci_msix(u8 off);

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
    pcicfg_msix _msix;

};

#endif
