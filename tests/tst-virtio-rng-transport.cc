/*
 * Copyright (C) 2026 Greg Burd
 * BSD license; see LICENSE in the top-level directory.
 */

// Opt-in: dedicated single-CPU guest, CPU RNG masked, one virtio RNG.
// argv[1] optionally specifies the decimal byte from a controlled host backend.
// A constant stream proves routing, NOT entropy quality. Without that argument,
// use an explicitly configured rng-random backend backed by host /dev/urandom.
#include <algorithm>
#include <atomic>
#include <cassert>
#include <chrono>
#include <cstdio>
#include <cstdlib>
#include <fcntl.h>
#include <thread>
#include <unistd.h>
#include <osv/pci.hh>
#include <drivers/pci-device.hh>
#include <vector>
#include <string>
// Test-only access to already-probed drivers; never reprobe a live device.
#define private public
#include <drivers/driver.hh>
#undef private
#include <drivers/virtio-rng.hh>
#include <osv/sched.hh>

int main(int argc, char** argv)
{
    assert(argc == 1 || argc == 2);
    assert(sched::cpus.size() == 1);
#ifdef __x86_64__
    unsigned a, b, c, d;
    asm volatile("cpuid" : "=a"(a), "=b"(b), "=c"(c), "=d"(d) : "a"(1), "c"(0));
    assert(!(c & (1u << 30))); // RDRAND unavailable
    asm volatile("cpuid" : "=a"(a), "=b"(b), "=c"(c), "=d"(d) : "a"(7), "c"(0));
    assert(!(b & (1u << 18))); // RDSEED unavailable
#endif
    std::atomic<bool> done{false};
    std::thread watchdog([&] {
        for (int i = 0; i < 300 && !done.load(); ++i) {
            std::this_thread::sleep_for(std::chrono::milliseconds(100));
        }
        assert(done.load() && "virtio RNG transport/devrandom deadline");
    });
    virtio::rng* rng = nullptr;
    for (auto* driver : hw::driver_manager::instance()->_drivers) {
        if (auto* candidate = dynamic_cast<virtio::rng*>(driver)) {
            assert(!rng && "test requires exactly one virtio RNG");
            rng = candidate;
        }
    }
    assert(rng && "virtio RNG was not probed");
    int expected = argc == 2 ? atoi(argv[1]) : -1;
    assert(expected >= -1 && expected <= 255);
    size_t received = 0;
    auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds(15);
    while (received < 256 && std::chrono::steady_clock::now() < deadline) {
        char bytes[64];
        size_t n = rng->get_random_bytes(bytes, sizeof(bytes));
        assert(n <= sizeof(bytes));
        if (expected >= 0) {
            for (size_t i = 0; i < n; ++i) {
                assert(static_cast<unsigned char>(bytes[i]) == expected &&
                       "virtqueue bytes differ from controlled backend");
            }
        }
        received += n;
        std::this_thread::sleep_for(std::chrono::milliseconds(10));
    }
    assert(received >= 256 && "no completed virtqueue source bytes");
    printf("PASS: %zu bytes consumed directly from probed virtio RNG%s\n",
           received, expected >= 0 ? "; exact backend byte matched" : "");
    // Availability only. Legacy interrupt credit can satisfy this independently;
    // the direct source-byte assertion above is the transport evidence.
    int fd = open("/dev/random", O_RDONLY);
    assert(fd >= 0);
    char output[32];
    assert(read(fd, output, sizeof(output)) == sizeof(output));
    assert(close(fd) == 0);
    puts("PASS: /dev/random available (not a secure-seed assertion)");
    done = true;
    watchdog.join();
}
