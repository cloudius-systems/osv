#include <algorithm>
#include <list>
#include <map>

#include "sched.hh"
#include "drivers/pci-function.hh"
#include "exceptions.hh"
#include "interrupt.hh"
#include "apic.hh"

using namespace pci;

interrupt_manager* interrupt_manager::_instance = nullptr;

msix_vector::msix_vector(pci_function* dev)
    : _num_entries (0), _dev(dev)
{
    _vector = idt.register_handler([this] { interrupt(); });
}

msix_vector::~msix_vector()
{
    idt.unregister_handler(_vector);
}

pci_function* msix_vector::get_pci_function(void)
{
    return (_dev);
}

unsigned msix_vector::get_vector(void)
{
    return (_vector);
}

void msix_vector::msix_unmask_entries(void)
{
    for (auto it=_entryids.begin(); it != _entryids.end(); it++) {
        int entry_id = (int)*it;
        _dev->msix_unmask_entry(entry_id);
    }
}

void msix_vector::msix_mask_entries(void)
{
    for (auto it=_entryids.begin(); it != _entryids.end(); it++) {
        int entry_id = (int)*it;
        _dev->msix_mask_entry(entry_id);
    }
}

void msix_vector::set_handler(std::function<void ()> handler)
{
    _handler = handler;
}

void msix_vector::add_entryid(unsigned entry_id)
{
    _entryids.push_back(entry_id);
}

void msix_vector::interrupt(void)
{
    _handler();
}

interrupt_manager::interrupt_manager()
{
    for (int i=0; i<256; i++) {
        _vectors[i] = nullptr;
    }
}

interrupt_manager::~interrupt_manager()
{

}

bool interrupt_manager::easy_register(pci::pci_function* dev, msix_isr_list& isrs)
{
    unsigned n = isrs.size();

    assigned_vectors assigned = request_vectors(dev, n);

    if (assigned._num != n) {
        free_vectors(assigned);
        return (false);
    }

    // Enable the device msix capability,
    // masks all interrupts...
    dev->msix_enable();

    int idx=0;

    for (auto it = isrs.begin(); it != isrs.end(); it++) {
        unsigned vec = assigned._vectors[idx++];
        sched::thread *isr = it->second;
        bool assign_ok = assign_isr(vec, [isr]{ isr->wake(); });
        if (!assign_ok) {
            free_vectors(assigned);
            return false;
        }
        bool setup_ok = setup_entry(it->first, vec);
        if (!setup_ok) {
            free_vectors(assigned);
            return false;
        }
    }

    // Save reference for assigned vectors
    _easy_dev2vectors.insert(std::make_pair(dev, assigned));
    unmask_interrupts(assigned);

    return (true);
}

void interrupt_manager::easy_unregister(pci::pci_function* dev)
{
    auto it = _easy_dev2vectors.find(dev);
    if (it != _easy_dev2vectors.end()) {
        free_vectors(it->second);
        _easy_dev2vectors.erase(it);
    }
}

assigned_vectors interrupt_manager::request_vectors(pci::pci_function* dev, int num_vectors)
{
    assigned_vectors results;
    unsigned ctr=0;

    results._num = std::min(num_vectors, dev->msix_get_num_entries());

    for (int i=0; i<results._num; i++) {
        msix_vector * msix = new msix_vector(dev);
        unsigned vector = msix->get_vector();
        _vectors[vector] = msix;

        results._vectors[ctr++] = vector;
    }

    return (results);
}

bool interrupt_manager::assign_isr(unsigned vector, std::function<void ()> handler)
{
    if (!_vectors[vector]) {
        return (false);
    }

    _vectors[vector]->set_handler(handler);
}

bool interrupt_manager::setup_entry(unsigned entry_id, unsigned vector)
{
    // vector must be allocated
    if (!_vectors[vector]) {
        return (false);
    }

    msix_vector* msix = _vectors[vector];
    pci_function* dev = msix->get_pci_function();

    u64 address;
    u32 data;

    if (!apic->compose_msix(vector, 0, address, data)) {
        return (false);
    }

    if (!dev->msix_write_entry(entry_id, address, data)) {
        return (false);
    }

    msix->add_entryid(entry_id);
    return (true);
}

void interrupt_manager::free_vectors(const assigned_vectors& vectors)
{
    for (int i=0; i<vectors._num; i++) {
        unsigned vec = vectors._vectors[i];
        if (_vectors[vec] != nullptr) {
            delete _vectors[vec];
            _vectors[vec] = nullptr;
        }
    }
}

bool interrupt_manager::unmask_interrupts(const assigned_vectors& vectors)
{
    for (int i=0; i<vectors._num; i++) {
        unsigned vec = vectors._vectors[i];
        msix_vector* msix = _vectors[vec];
        if (msix == nullptr) {
            continue;
        }

        msix->msix_unmask_entries();
    }

    return (true);
}
