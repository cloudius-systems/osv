#include "drivers/driver.hh"
#include "drivers/pci.hh"
#include "debug.hh"

using namespace pci;

bool Driver::isPresent() {
    return _present;
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

    debug(fmt("Driver:Init %x:%x") % _vid % _id);

    setBusMaster(true);

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

        u8 capability = pci_readb(off + PCI_CAP_OFF_ID);
        switch (capability) {
        case PCI_CAP_MSIX:
            _have_msix = true;
            debug(fmt("Have MSI-X!"));
            break;
        }

        off = pci_readb(off + PCI_CAP_OFF_NEXT);
    }

    return true;
}

bool Driver::parse_pci_msix(void)
{
    return true;
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
