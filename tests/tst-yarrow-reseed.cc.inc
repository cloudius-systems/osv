// Copyright (C) 2026 Greg Burd
// BSD license; see LICENSE in the top-level directory.
#include <algorithm>
#include <atomic>
#include <cstdio>
#include <cerrno>
#include <fcntl.h>
#include <mutex>
#include <thread>
#include <unistd.h>
#include <sys/wait.h>
#define __OSV__
#define _KERNEL
#define RANDOM_YARROW
#define MTX_DEF 0
#define PUSER 0
#define PCATCH 0
// KERNEL_HEADERS
struct mtx { std::mutex lock; };
static void mtx_init(mtx*, const char*, void*, int) {}
static void mtx_destroy(mtx*) {}
static thread_local bool observe_lock;
static std::atomic<bool> lock_attempted{false};
static void mtx_lock(mtx* m) {
    if (observe_lock) lock_attempted = true;
    m->lock.lock();
}
static void mtx_unlock(mtx* m) { m->lock.unlock(); }
static struct { int seeded; } random_context;
[[maybe_unused]] static auto* random_adaptor = &random_context;
static int wakes;
static void wakeup(void*) { ++wakes; }
static void debug(const char*) {}
static int msleep(void*, mtx*, int, const char*, int) { assert(false); return 0; }
static int random_kthread_control;
static int hz = 100;
static struct harvest pending;
static bool have_pending;
static void bsd_pause(const char*, int) {
    // Complete the flush handshake without a scheduler or live HW source.
    if (have_pending) { random_process_event(&pending); have_pending = false; }
    random_kthread_control = 0;
}
static thread_local unsigned encryptions;
static void counted_encrypt(randomdev_key* key, void* in, void* out, unsigned n) {
    ++encryptions;
    randomdev_encrypt(key, in, out, n);
}
#define randomdev_encrypt counted_encrypt
// KERNEL_CODE
#undef randomdev_encrypt
static void queue_event(unsigned bits, uint64_t material) {
    memset(pending.entropy, 0, sizeof(pending.entropy));
    pending.somecounter = material;
    pending.size = sizeof(material);
    pending.bits = bits;
    pending.source = RANDOM_PURE_RDRAND;
    memcpy(pending.entropy, &material, sizeof(material));
    have_pending = true;
}
static void flush(unsigned bits, uint64_t material) {
    queue_event(bits, material);
    randomdev_flush_reseed();
}
static void read_all(uint8_t* out, int n) {
    while (n) { int got = random_yarrow_read(out, n); assert(got > 0); out += got; n -= got; }
}
static void unseeded() {
    assert(randomdev_block(O_NONBLOCK) == EWOULDBLOCK);
    for (int i = 0; i < 3; ++i) {
        flush(0, i + 1);
        assert(random_context.seeded == 0 && wakes == 0);
        assert(randomdev_block(O_NONBLOCK) == EWOULDBLOCK);
    }
    // Explicit flushes must preserve sub-threshold credit. The selector is
    // now SLOW: two 64-bit FAST events, with a zero-credit event between,
    // cross its 96-bit threshold; none of the earlier flushes may unblock.
    flush(64, 42);
    flush(64, 44);
    flush(0, 45);
    assert(!random_context.seeded && wakes == 0);
    flush(64, 46);
    assert(random_context.seeded && randomdev_block(O_NONBLOCK) == 0 && wakes == 1);
    flush(0, 43);
    assert(random_context.seeded && wakes == 1);
    puts("PASS: unseeded zero-credit flush blocked; credited seeding; seeded rekey");
}
static void tail() {
    flush(256, 42);
    assert(random_context.seeded);
    for (int prefix = 1; prefix < BLOCKSIZE; ++prefix) {
        uint8_t discard[BLOCKSIZE];
        read_all(discard, BLOCKSIZE); // discard any previous partial block
        read_all(discard, prefix);
        // Fork captures random_state AND function-static cur/genval on old code.
        uint8_t output[2][32]{};
        for (int clone = 0; clone < 2; ++clone) {
            int pipefd[2]; assert(pipe(pipefd) == 0);
            pid_t pid = fork(); assert(pid >= 0);
            if (!pid) {
                close(pipefd[0]);
                flush(0, 1000 + clone);
                uint8_t bytes[32];
                unsigned before = encryptions;
                read_all(bytes, 1);
                // A cached pre-reseed byte performs no encryption at all.
                assert(encryptions > before);
                read_all(bytes + 1, 31);
                assert(write(pipefd[1], bytes, sizeof(bytes)) == sizeof(bytes));
                _exit(0);
            }
            close(pipefd[1]);
            assert(read(pipefd[0], output[clone], 32) == 32);
            close(pipefd[0]);
            int status; assert(waitpid(pid, &status, 0) == pid && WIFEXITED(status) && !WEXITSTATUS(status));
        }
        assert(memcmp(output[0], output[1], 32) != 0);
    }
    puts("PASS: fork-copied Yarrow partial tails 1..15 diverge after distinct rekeys");
}
static void serialization() {
    // A zero-credit event must not update pool hashes while reseed/read owns the lock.
    queue_event(0, 99);
    mtx_lock(&random_reseed_mtx);
    std::atomic<bool> done{false};
    std::thread worker([&] {
        observe_lock = true;
        random_process_event(&pending);
        done = true;
    });
    // Fixed code reaches the held mutex; old code finishes without attempting
    // it. Neither result depends on scheduling delay. Outer runner bounds hangs.
    while (!lock_attempted && !done) std::this_thread::yield();
    bool escaped = done.load();
    mtx_unlock(&random_reseed_mtx);
    worker.join();
    assert(!escaped && done);
    flush(256, 42);
    flush(256, 43);
    std::thread reader([] {
        uint8_t bytes[17];
        for (int i = 0; i < 1000; ++i) read_all(bytes, (i % 17) + 1);
    });
    std::thread reseeder([] {
        for (int i = 0; i < 1000; ++i) random_yarrow_reseed();
    });
    for (int i = 0; i < 1000; ++i) random_process_event(&pending);
    reader.join(); reseeder.join();
    puts("PASS: event pool updates serialized; concurrent read/event/rekey stress");
}
int main(int argc, char** argv) {
    assert(argc == 2);
    random_yarrow_init_alg();
    if (!strcmp(argv[1], "unseeded")) unseeded();
    else if (!strcmp(argv[1], "tail")) tail();
    else if (!strcmp(argv[1], "serialization")) serialization();
    else assert(false);
    random_yarrow_deinit_alg();
}
