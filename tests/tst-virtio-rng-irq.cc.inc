// Copyright (C) 2026 Greg Burd
// BSD license; see LICENSE in the top-level directory.
#include <algorithm>
#include <cassert>
#include <cstdio>
#include <functional>
#include <iterator>
#include <memory>
#include <string>
#include <vector>
using namespace std;
using u8 = unsigned char;
static unsigned wakes, disables, reads, writes, started, registered;
struct mutex {};
struct condvar { void wake_one() {} };
#define WITH_LOCK(x) if (true)
namespace sched {
struct thread {
    struct attr { attr& name(const char*) { return *this; } };
    static thread* make(function<void()>, attr) { return new thread; }
    void start() { ++started; }
    void wake_with_irq_disabled() { ++wakes; }
};
}
namespace pci {
struct device { bool msix = false; bool is_msix() { return msix; } };
}
namespace gic { enum class irq_type { IRQ_TYPE_EDGE }; }
struct irq {
    function<bool()> ack;
    function<void()> handler;
    irq(function<bool()> a, function<void()> h): ack(a), handler(h) {}
    void fire() { if (ack()) handler(); }
};
struct pci_interrupt : irq {
    pci_interrupt(pci::device&, function<bool()> a, function<void()> h): irq(a,h) {}
};
struct spi_interrupt : irq {
    spi_interrupt(gic::irq_type t, unsigned n, function<bool()> a, function<void()> h): irq(a,h) {
        assert(t == gic::irq_type::IRQ_TYPE_EDGE && n == 37);
    }
};
struct gsi_edge_interrupt : irq {
    gsi_edge_interrupt(unsigned n, function<void()> h): irq([] { return true; },h) { assert(n == 37); }
};
struct msix_binding { unsigned entry; function<void()> isr; sched::thread* t; };
struct interrupt_manager {
    vector<msix_binding> bindings;
    bool easy_register(initializer_list<msix_binding> b) { bindings = b; return true; }
};
struct hw_device { virtual ~hw_device() = default; };
struct hw_driver { virtual ~hw_driver() = default; };
struct vring { void disable_interrupts() { ++disables; } };
namespace virtio {
// FACTORY
struct virtio_device : hw_device {
    virtual unsigned get_irq() { return 37; }
    virtual u8 read_and_ack_isr() = 0;
    virtual void register_interrupt(interrupt_factory) = 0;
};
#define VIRTIO_MMIO_INTERRUPT_STATUS 0x60
#define VIRTIO_MMIO_INTERRUPT_ACK 0x64
#define VIRTIO_MMIO_INT_VRING 1
static unsigned status, last_ack;
static unsigned mmio_getl(unsigned addr) { assert(addr == 0x60); ++reads; return status; }
static void mmio_setl(unsigned addr, unsigned value) {
    assert(addr == 0x64); ++writes; last_ack = value; status &= ~value;
}
struct mmio_device : virtio_device {
    unsigned _addr_mmio = 0;
#ifdef __aarch64__
    unique_ptr<spi_interrupt> _irq;
#else
    unique_ptr<gsi_edge_interrupt> _irq;
#endif
    void register_interrupt(interrupt_factory) override;
    u8 read_and_ack_isr() override;
};
struct virtio_pci_device : virtio_device {
    pci::device pci_dev;
    pci::device* _dev = &pci_dev;
    unique_ptr<pci_interrupt> _irq;
    interrupt_manager _msi;
    bool pending = false;
    void register_interrupt(interrupt_factory) override;
    u8 read_and_ack_isr() override { ++reads; bool p = pending; pending = false; return p; }
};
struct virtio_driver : hw_driver {
    virtio_device& _dev;
    vring queue;
    explicit virtio_driver(virtio_device& d): _dev(d) {}
    void setup_features() {}
    void probe_virt_queues() {}
    vring* get_virt_queue(unsigned n) { assert(n == 0); return &queue; }
    void add_dev_status(unsigned n) { assert(n == 4 && started == 0); }
};
constexpr unsigned VIRTIO_CONFIG_S_DRIVER_OK = 4;
constexpr unsigned VIRTIO_ID_RNG = 4;
template<class T, unsigned ID> hw_driver* probe(hw_device* dev) {
    static_assert(ID == 4);
    auto* v = dynamic_cast<virtio_device*>(dev);
    return v ? new T(*v) : nullptr;
}
}
namespace randomdev {
struct hw_rng { virtual size_t get_random_bytes(char*, size_t) = 0; };
}
// RNG_HEADER
static randomdev::hw_rng* s_hwrng;
static int vrng;
static void live_entropy_source_register(int*) { assert(started == 1 && s_hwrng); ++registered; }
static void live_entropy_source_deregister(int*) { assert(s_hwrng); --registered; }
void virtio::rng::worker() { assert(false); } // no fake completion/worker proof
// ACTUAL_CODE
int main(int argc, char** argv) {
    assert(argc == 2);
    string mode = argv[1];
    virtio::mmio_device mmio;
    virtio::virtio_pci_device pci;
    pci.pci_dev.msix = mode == "msix";
    hw_device* dev = mode == "mmio" ? static_cast<hw_device*>(&mmio) : &pci;
    unique_ptr<hw_driver> driver;
    try {
        driver.reset(virtio::rng::probe(dev));
    } catch (const bad_function_call&) {
        fputs("FAIL: RNG probe invoked an empty MMIO interrupt factory\n", stderr);
        assert(false && "RNG MMIO interrupt factory missing");
    }
    assert(driver && started == 1 && registered == 1);
    assert(wakes == 0 && reads == 0 && writes == 0);
    char byte = 0x55;
    assert(s_hwrng->get_random_bytes(&byte, 1) == 0 && byte == 0x55);
    if (mode == "mmio") {
        assert(mmio._irq);
        // Exact device ISR semantics: acknowledge observed status, but wake only
        // for VRING, never zero/spurious or config-only. Repeat after ctor so
        // accidental dangling-reference captures are exercised.
        for (unsigned s : {0u, 2u, 1u, 3u}) {
            unsigned before = wakes;
            virtio::status = s;
            mmio._irq->fire();
            assert(virtio::last_ack == s && virtio::status == 0);
            assert(wakes == before + (s & 1));
        }
        assert(reads == 4 && writes == 4 && wakes == 2 && disables == 0);
    } else if (mode == "pci") {
        assert(pci._irq && pci._msi.bindings.empty());
        pci._irq->fire();
        assert(wakes == 0 && reads == 1);
        pci.pending = true;
        pci._irq->fire();
        assert(wakes == 1 && reads == 2 && !pci.pending && writes == 0);
    } else {
        assert(mode == "msix" && !pci._irq && pci._msi.bindings.size() == 1);
        auto& b = pci._msi.bindings[0];
        assert(b.entry == 0 && b.t);
        b.isr();
        b.t->wake_with_irq_disabled();
        assert(disables == 1 && wakes == 1 && reads == 0 && writes == 0);
    }
    // Interrupts without completed buffers must not manufacture source bytes.
    assert(s_hwrng->get_random_bytes(&byte, 1) == 0 && byte == 0x55);
    driver.reset();
    assert(!s_hwrng && !registered);
    puts("PASS: real RNG callbacks, transport selection, ACK and wake controls");
}
