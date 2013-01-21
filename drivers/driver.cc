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

#include "drivers/driver.hh"
#include "drivers/pci.hh"
#include "debug.hh"

using namespace pci;

bool Driver::isPresent() {
    return _present;
}

Driver::~Driver() {
    //todo - Better change the Bar allocation to live in the stack
    for (int i=0;i<6;i++) if (_bars[i]) delete _bars[i];
}

void
Driver::setPresent(u8 bus, u8 slot, u8 func) {
    _present = true;
    _bus = bus;
    _slot = slot;
    _func = func;
}

u16 Driver::getStatus() {
    if (!_present) throw new InitException();

    return read_pci_config_word(_bus,_slot,_func,PCI_STATUS_OFFSET);
}

void Driver::setStatus(u16 s) {
    if (!_present) throw new InitException();

    write_pci_config_word(_bus,_slot,_func,PCI_STATUS_OFFSET,s);
}

u16 Driver::get_command(void)
{
    return (pci_readw(PCI_COMMAND_OFFSET));
}

void Driver::set_command(u16 c)
{
    pci_writew(PCI_COMMAND_OFFSET, c);
}


u8
Driver::getRevision() {
    if (!_present) throw new InitException();

    return read_pci_config_byte(_bus,_slot,_func,PCI_CLASS_REVISION);

}

u16
Driver::getSubsysId() {
    if (!_present) throw new InitException();

    return read_pci_config_word(_bus,_slot,_func,PCI_SUBSYSTEM_ID);
}

u16
Driver::getSubsysVid() {
    if (!_present) throw new InitException();

    return read_pci_config_word(_bus,_slot,_func,PCI_SUBSYSTEM_VID);
}

bool
Driver::getBusMaster() {
    u16 command = read_pci_config_word(_bus,_slot,_func, PCI_COMMAND_OFFSET);
    return (command & (1 << PCI_BUS_MASTER_BIT));
}

void
Driver::setBusMaster(bool master) {
    u16 command = read_pci_config_word(_bus,_slot,_func, PCI_COMMAND_OFFSET);
    command = (master)? command |(1 << PCI_BUS_MASTER_BIT) :
                        command & ~(1 << PCI_BUS_MASTER_BIT);
    write_pci_config_word(_bus,_slot,_func, PCI_COMMAND_OFFSET, command);
}

bool
Driver::pciEnable() {
    return _present;
}

bool
Driver::allocateBARs() {
    return true;
}

bool
Driver::earlyInitChecks() {
    initBars();

    return _present;
}

bool
Driver::Init(Device* dev) {
    if (!dev) return false;

    if (!earlyInitChecks()) {
        return false;
    }

    parse_pci_config();

    setBusMaster(true);

    // Enable MSI-x
    if (_have_msix) {
        if (is_intx_enabled()) {
            disable_intx();
        }
    }

    debug(fmt("Driver initialized %x:%x") % _vid % _id);
    return true;
}

void
Driver::initBars() {
    for (int i=0; i<6; i++)
        _bars[i] = new Bar(i, this);
}

u8
Driver::getBus() {
    if (!_present) throw new InitException();
    return _bus;
}

u8
Driver::getSlot() {
    if (!_present) throw new InitException();
    return _slot;
}

u8
Driver::getFunc() {
    if (!_present) throw new InitException();
    return _func;
}

bool Driver::parse_pci_config(void)
{
    // Parse capabilities
    bool parse_ok = parse_pci_capabilities();

    return parse_ok;
}

bool Driver::parse_pci_capabilities(void)
{
    // FIXME: check pci device type (act differently if bridge)
    u8 capabilities_base = pci_readb(PCI_CAPABILITIES_PTR);
    u8 off = capabilities_base;

    while (off != 0) {
        if (off > 255) {
            return (false);
        }

        // Read capability
        u8 capability = pci_readb(off + PCI_CAP_OFF_ID);
        switch (capability) {
        case PCI_CAP_MSIX:
            _have_msix = true;
            parse_pci_msix(off);
            break;
        }

        off = pci_readb(off + PCI_CAP_OFF_NEXT);
    }

    return true;
}

bool Driver::parse_pci_msix(u8 off)
{
    // Used for parsing MSI-x
    u32 val = 0;

    // Location within the configuration space
    _msix.msix_location = off;
    _msix.msix_ctrl = pci_readw(off + PCIR_MSIX_CTRL);
    _msix.msix_msgnum = (_msix.msix_ctrl & PCIM_MSIXCTRL_TABLE_SIZE) + 1;
    val = pci_readl(off + PCIR_MSIX_TABLE);
    _msix.msix_table_bar = val & PCIM_MSIX_BIR_MASK;
    _msix.msix_table_offset = val & ~PCIM_MSIX_BIR_MASK;
    val = pci_readl(off + PCIR_MSIX_PBA);
    _msix.msix_pba_bar = val & PCIM_MSIX_BIR_MASK;
    _msix.msix_pba_offset = val & ~PCIM_MSIX_BIR_MASK;

    debug(fmt("Have MSI-X!"));
    debug(fmt("    msix_location: %1%") % (u16)_msix.msix_location);
    debug(fmt("    msix_ctrl: %1%") % _msix.msix_ctrl);
    debug(fmt("    msix_msgnum: %1%") % _msix.msix_msgnum);
    debug(fmt("    msix_table_bar: %1%") % (u16)_msix.msix_table_bar);
    debug(fmt("    msix_table_offset: %1%") % _msix.msix_table_offset);
    debug(fmt("    msix_pba_bar: %1%") % (u16)_msix.msix_pba_bar);
    debug(fmt("    msix_pba_offset: %1%") % _msix.msix_pba_offset);

    return true;
}

bool Driver::is_intx_enabled(void)
{
    u16 command = get_command();
    return ( (command & PCI_COMMAND_INTX_DISABLE) == 0 );
}

void Driver::enable_intx(void)
{
    u16 command = get_command();
    command &= ~PCI_COMMAND_INTX_DISABLE;
    set_command(command);
}

void Driver::disable_intx(void)
{
    u16 command = get_command();
    command |= PCI_COMMAND_INTX_DISABLE;
    set_command(command);
}

u8 Driver::pci_readb(u8 offset)
{
    return read_pci_config_byte(_bus, _slot, _func, offset);
}

u16 Driver::pci_readw(u8 offset)
{
    return read_pci_config_word(_bus, _slot, _func, offset);
}

u32 Driver::pci_readl(u8 offset)
{
    return read_pci_config(_bus, _slot, _func, offset);
}

void Driver::pci_writeb(u8 offset, u8 val)
{
    write_pci_config_byte(_bus, _slot, _func, offset, val);
}

void Driver::pci_writew(u8 offset, u16 val)
{
    write_pci_config_word(_bus, _slot, _func, offset, val);
}

void Driver::pci_writel(u8 offset, u32 val)
{
    write_pci_config(_bus, _slot, _func, offset, val);
}

void Driver::dumpConfig() const {
    debug(fmt("Driver vid:id= %x:%x") % _vid % _id);
}

std::ostream& operator << (std::ostream& out, const Driver& d) {
   out << "Driver dev id=" << d._id << " vid=" << d._vid << std::endl;
   return out;
}
