#!/usr/bin/env python3
# Copyright (C) 2026 Greg Burd
# BSD license as described in the top-level LICENSE file.
"""Host fault injection against actual libext lifecycle bodies, not a model.

Run: python3 tests/tst-ext4-workers.py
Only OSv allocation/mutex and lwext4 I/O boundaries are substituted. Real
pthreads execute the production worker. A read is held until teardown joins,
and destruction is rejected if any successful worker remains unjoined.
"""
import os
from pathlib import Path
import subprocess
import tempfile

root = Path(__file__).resolve().parents[1]
source = (root / 'modules/libext/ext_vnops.cc').read_text()


def section(start, end):
    return source[source.index(start):source.index(end, source.index(start))]


prefix = r'''
#include <pthread.h>
#include <atomic>
#include <cassert>
#include <cerrno>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <thread>
#include <vector>
using mutex_t = pthread_mutex_t;
static void mutex_init(mutex_t* m) { assert(!pthread_mutex_init(m, nullptr)); }
static void free_contiguous_aligned(void* p) { free(p); }
struct ext4_fs {};
struct ext4_inode_ref {};
struct auto_inode_ref {
    ext4_inode_ref _ref;
    int _r = 0;
    auto_inode_ref(ext4_fs*, uint32_t) {}
};
#define EOK 0
static std::atomic<unsigned> entered{0}, completed{0};
static std::atomic<bool> release_read{false};
static int ext_internal_read(ext4_fs*, ext4_inode_ref*, uint64_t,
                             void* buf, size_t size, size_t* got) {
    ++entered;
    while (!release_read.load()) std::this_thread::yield();
    memset(buf, 0x5a, size);
    *got = size;
    ++completed;
    return 0;
}
static unsigned fail_mask, attempts, joined;
static std::vector<pthread_t> created;
static int create_worker(pthread_t* out, const pthread_attr_t* attr,
                         void* (*fn)(void*), void* arg) {
    if (fail_mask & (1u << attempts++)) {
        // POSIX leaves the output undefined on failure. Do not rely on zero.
        *out = pthread_self();
        return EAGAIN;
    }
    int r = pthread_create(out, attr, fn, arg);
    assert(!r);
    created.push_back(*out);
    return r;
}
static int join_worker(pthread_t id, void** result) {
    bool valid = false;
    for (auto t : created) valid |= pthread_equal(t, id);
    if (!valid) {
        fprintf(stderr, "FAIL: teardown tried to join an unsuccessful slot\n");
        return ESRCH;
    }
    release_read = true;
    int r = pthread_join(id, result);
    assert(!r);
    ++joined;
    return r;
}
static int destroy_cond(pthread_cond_t* cv) {
    if (joined != created.size()) {
        fprintf(stderr, "FAIL: destroying state with %zu workers unjoined\n",
                created.size() - joined);
        abort();
    }
    return pthread_cond_destroy(cv);
}
#define pthread_create create_worker
#define pthread_join join_worker
#define pthread_cond_destroy destroy_cond
'''
body = section('static const size_t   RA_WINDOW', '\ntypedef\tstruct vnode')
body += section('static void *ext_prefetch_worker(', '\n// Arm ring slot')
suffix = r'''
int main(int argc, char** argv) {
    assert(argc == 2);
    fail_mask = strtoul(argv[1], nullptr, 0);
    auto v = new ext_vdata;
    ext4_fs fs;
    unsigned expected = 0;
    for (unsigned i = 0; i < RA_WORKERS; ++i)
        expected += !(fail_mask & (1u << i));
    assert(ext_ensure_workers(v, &fs, 17) == (expected != 0));
    assert(v->nworkers == expected);
    assert(attempts == RA_WORKERS); // continue attempting after a failure
    if (expected) {
        // Every survivor must run real work, including the last attempted slot.
        pthread_mutex_lock(&v->ra_cvmtx);
        for (unsigned i = 0; i < expected; ++i) {
            v->win_buf[i] = static_cast<char*>(malloc(16));
            assert(v->win_buf[i]);
            v->win_state[i] = RA_FILLING;
            v->jobq[v->jobq_n++] = {i, 0, 16, v->gen};
            ++v->inflight;
        }
        pthread_cond_broadcast(&v->ra_job_cv);
        pthread_mutex_unlock(&v->ra_cvmtx);
        while (entered.load() != expected) std::this_thread::yield();
        assert(!release_read); // read completes only after destructor starts
        assert(ext_ensure_workers(v, &fs, 17));
        assert(attempts == RA_WORKERS); // successful partial pool is reused
    }
    delete v; // actual production destructor, including cond/buffer destruction
    assert(joined == expected);
    assert(completed == expected);
    printf("PASS failure mask=%u: %u live readers joined before destruction\n",
           fail_mask, expected);
}
'''
with tempfile.TemporaryDirectory(prefix='tst-ext4-workers-') as tmp:
    cpp = Path(tmp) / 'workers.cc'
    exe = Path(tmp) / 'workers'
    cpp.write_text(prefix + body + suffix)
    subprocess.run([os.environ.get('CXX', 'g++'), '-std=c++11', '-Wall', '-Wextra',
                    '-Werror', '-pthread', '-fsanitize=address,undefined',
                    '-fno-omit-frame-pointer', str(cpp), '-o', str(exe)], check=True)
    failed = []
    for mask in range(16):
        p = subprocess.run([str(exe), str(mask)], timeout=15)
        if p.returncode:
            failed.append(mask)
    assert not failed, f'Worker lifecycle failed for masks {failed}'
