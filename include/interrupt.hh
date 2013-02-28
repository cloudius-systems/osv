#ifndef INTERRUPT_HH_
#define INTERRUPT_HH_

#include <functional>
#include <map>
#include <list>

#include "sched.hh"
#include "drivers/pci.hh"
#include "drivers/pci-function.hh"
#include <osv/types.h>

// max vectors per request
const int max_vectors = 256;

class msix_vector {
public:
    msix_vector(pci::pci_function* dev);
    virtual ~msix_vector();

    pci::pci_function* get_pci_function(void);
    unsigned get_vector(void);
    void msix_unmask_entries(void);
    void msix_mask_entries(void);

    void add_entryid(unsigned entry_id);
    void interrupt(void);
    void set_handler(std::function<void ()> handler);

private:
    // Handler to invoke...
    std::function<void ()> _handler;
    // The device that owns this vector
    pci::pci_function * _dev;
    // Entry ids used by this vector
    std::list<unsigned> _entryids;
    unsigned _vector;
};

// entry -> thread to wake
typedef std::map<unsigned, sched::thread *> msix_isr_list;

class interrupt_manager {
public:

    explicit interrupt_manager(pci::pci_function* dev);
    ~interrupt_manager();

    ////////////////////
    // Easy Interface //
    ////////////////////

    // 1. Enabled MSI-x For device
    // 2. Allocate vectors and assign ISRs
    // 3. Setup entries
    // 4. Unmask interrupts
    bool easy_register(msix_isr_list& isrs);
    void easy_unregister();

    /////////////////////
    // Multi Interface //
    /////////////////////

    std::vector<msix_vector*> request_vectors(unsigned num_vectors);
    void free_vectors(const std::vector<msix_vector*>& vectors);
    bool assign_isr(msix_vector*, std::function<void ()> handler);
    // Multiple entry can be assigned the same vector
    bool setup_entry(unsigned entry_id, msix_vector* vector);
    // unmasks all interrupts
    bool unmask_interrupts(const std::vector<msix_vector*>& vectors);

private:
    pci::pci_function* _dev;
    // Used by the easy interface
    std::vector<msix_vector*> _easy_vectors;
};

class inter_processor_interrupt {
public:
    explicit inter_processor_interrupt(std::function<void ()>);
    ~inter_processor_interrupt();
    void send(sched::cpu* cpu);
private:
    unsigned _vector;
};

#endif /* INTERRUPT_HH_ */
