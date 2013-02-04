#ifndef INTERRUPT_HH_
#define INTERRUPT_HH_

#include <functional>
#include <map>
#include <list>

#include "sched.hh"
#include "drivers/pci.hh"
#include "drivers/pci-function.hh"
#include "types.hh"

using namespace pci;

// max vectors per request
const int max_vectors = 256;

class msix_vector {
public:
    msix_vector(pci_function* dev);
    virtual ~msix_vector();

    pci_function* get_pci_function(void);
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
    pci_function * _dev;
    // Entry ids used by this vector
    std::list<unsigned> _entryids;
    unsigned _vector;
};


// Used to communicate assigned vectors back to driver
class assigned_vectors {
public:
    assigned_vectors() : _num(0) {
        for (int i=0; i<max_vectors; i++) {
            _vectors[i] = 0;
        }
    }
    unsigned _vectors[max_vectors];
    unsigned _num;
};

// entry -> thread to wake
typedef std::map<unsigned, sched::thread *> msix_isr_list;

class interrupt_manager {
public:

    interrupt_manager();
    virtual ~interrupt_manager();

    static interrupt_manager * instance() {
        if (_instance == nullptr) {
            _instance = new interrupt_manager();
        }

        return (_instance);
    }

    ////////////////////
    // Easy Interface //
    ////////////////////

    // 1. Enabled MSI-x For device
    // 2. Allocate vectors and assign ISRs
    // 3. Setup entries
    // 4. Unmask interrupts
    bool easy_register(pci_function* dev, msix_isr_list& isrs);
    void easy_unregister(pci_function* dev);

    /////////////////////
    // Multi Interface //
    /////////////////////

    assigned_vectors request_vectors(pci_function* dev, unsigned num_vectors);
    void free_vectors(const assigned_vectors& vectors);
    bool assign_isr(unsigned vector, std::function<void ()> handler);
    // Multiple entry can be assigned the same vector
    bool setup_entry(unsigned entry_id, unsigned vector);
    // unmasks all interrupts
    bool unmask_interrupts(const assigned_vectors& vectors);

private:
    static interrupt_manager* _instance;
    msix_vector* _vectors[max_vectors];
    // Used by the easy interface
    std::map<pci_function *, assigned_vectors> _easy_dev2vectors;
};

#endif /* INTERRUPT_HH_ */
