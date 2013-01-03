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

void Driver::dumpConfig() const {
    debug(fmt("Driver vid:id= %x:%x") % _vid % _id);
}

std::ostream& operator << (std::ostream& out, const Driver& d) {
   out << "Driver dev id=" << d._id << " vid=" << d._vid << std::endl;
   return out;
}
