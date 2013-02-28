#include <algorithm>
#include <list>
#include <map>

#include "sched.hh"
#include "drivers/pci-function.hh"
#include "exceptions.hh"
#include "interrupt.hh"
#include "apic.hh"

using namespace pci;

msix_vector::msix_vector(pci_function* dev)
    : _dev(dev)
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
    for (auto entry_id : _entryids) {
        _dev->msix_unmask_entry(entry_id);
    }
}

void msix_vector::msix_mask_entries(void)
{
    for (auto entry_id : _entryids) {
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

interrupt_manager::interrupt_manager(pci::pci_function* dev)
    : _dev(dev)
{
    for (int i=0; i<256; i++) {
        _vectors[i] = nullptr;
    }
}

interrupt_manager::~interrupt_manager()
{

}

bool interrupt_manager::easy_register(msix_isr_list& isrs)
{
    unsigned n = isrs.size();

    assigned_vectors assigned = request_vectors(n);

    if (assigned._num != n) {
        free_vectors(assigned);
        return (false);
    }

    // Enable the device msix capability,
    // masks all interrupts...
    _dev->msix_enable();

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
    _easy_vectors = assigned;
    unmask_interrupts(assigned);

    return (true);
}

void interrupt_manager::easy_unregister()
{
    free_vectors(_easy_vectors);
    _easy_vectors = assigned_vectors{};
}

assigned_vectors interrupt_manager::request_vectors(unsigned num_vectors)
{
    assigned_vectors results;
    unsigned ctr=0;

    results._num = std::min(num_vectors, _dev->msix_get_num_entries());

    for (unsigned i=0; i<results._num; i++) {
        msix_vector * msix = new msix_vector(_dev);
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

    return (true);
}

bool interrupt_manager::setup_entry(unsigned entry_id, unsigned vector)
{
    // vector must be allocated
    if (!_vectors[vector]) {
        return (false);
    }

    msix_vector* msix = _vectors[vector];

    msi_message msix_msg = apic->compose_msix(vector, 0);

    if (msix_msg._addr == 0) {
        return (false);
    }

    if (!_dev->msix_write_entry(entry_id, msix_msg._addr, msix_msg._data)) {
        return (false);
    }

    msix->add_entryid(entry_id);
    return (true);
}

void interrupt_manager::free_vectors(const assigned_vectors& vectors)
{
    for (unsigned i=0; i<vectors._num; i++) {
        unsigned vec = vectors._vectors[i];
        if (_vectors[vec] != nullptr) {
            delete _vectors[vec];
            _vectors[vec] = nullptr;
        }
    }
}

bool interrupt_manager::unmask_interrupts(const assigned_vectors& vectors)
{
    for (unsigned i=0; i<vectors._num; i++) {
        unsigned vec = vectors._vectors[i];
        msix_vector* msix = _vectors[vec];
        if (msix == nullptr) {
            continue;
        }

        msix->msix_unmask_entries();
    }

    return (true);
}

inter_processor_interrupt::inter_processor_interrupt(std::function<void ()> handler)
    : _vector(idt.register_handler(handler))
{
}

inter_processor_interrupt::~inter_processor_interrupt()
{
    idt.unregister_handler(_vector);
}

void inter_processor_interrupt::send(sched::cpu* cpu)
{
    apic->ipi(cpu->arch.apic_id, _vector);
}
