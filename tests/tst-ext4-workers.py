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
#include <cstddef>
#include <algorithm>
using mutex_t = pthread_mutex_t;
static void mutex_init(mutex_t* m) { assert(!pthread_mutex_init(m, nullptr)); }
static void free_contiguous_aligned(void* p) { free(p); }
static void mutex_lock(mutex_t* m) { assert(!pthread_mutex_lock(m)); }
static void mutex_unlock(mutex_t* m) { assert(!pthread_mutex_unlock(m)); }
static void* alloc_contiguous_aligned(size_t n, size_t) { return malloc(n); }
struct ext4_fs { int sb; };
struct ext4_inode { uint64_t size; };
struct ext4_inode_ref { ext4_inode* inode; };
struct mount { void* m_data; };
struct vnode { mount* v_mount; uint32_t v_ino; void* v_data; };
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
static pthread_cond_t* drain_cv;
static int wait_cond(pthread_cond_t* cv, pthread_mutex_t* m) {
    if (cv == drain_cv) release_read = true;
    return pthread_cond_wait(cv, m);
}
static bool inode_error;
static ext4_inode inode{1048576};
static int ext4_fs_get_inode_ref(ext4_fs*, uint32_t, ext4_inode_ref* ref) {
    if (entered != completed) {
        fprintf(stderr, "FAIL: inode accessed for truncation with live prefetch read\n");
        abort();
    }
    ref->inode = &inode;
    return inode_error ? EIO : 0;
}
static int ext4_fs_put_inode_ref(ext4_inode_ref*) { return 0; }
static uint64_t ext4_inode_get_size(int*, ext4_inode* i) { return i->size; }
static int ext4_fs_truncate_inode(ext4_inode_ref* ref, uint64_t size) {
    ref->inode->size = size;
    return 0;
}
static int ext_internal_write(ext4_fs*, ext4_inode_ref*, uint64_t,
                              void*, size_t, size_t*) { return EIO; }
#define CONFIG_MAX_TRUNCATE_SIZE 262144
#define ext_debug(...)
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
#define pthread_cond_wait wait_cond
#define pthread_create create_worker
#define pthread_join join_worker
#define pthread_cond_destroy destroy_cond
'''
body = section('static const size_t   RA_WINDOW', '\ntypedef\tstruct vnode')
body += section('static void *ext_prefetch_worker(', '\n// Arm ring slot')
body += section('static void ext_ra_invalidate(', '\n// Copy read_amt')
body += section('static int\next_trunc_inode(', '\nstatic int\next_dir_trunc')
# Accept either signature so this regression also runs against the unfixed head.
trunc_call = ('ext_trunc_inode(&vp, 0, &updated)' if
              'ext_trunc_inode(struct vnode *vp,' in source else
              'ext_trunc_inode(&fs, 17, 0, &updated)')
suffix = r'''
int main(int argc, char** argv) {
    (void)ext_ra_invalidate;
    (void)mutex_lock;
    (void)mutex_unlock;
    assert(argc >= 2 && argc <= 4);
    fail_mask = strtoul(argv[1], nullptr, 0);
    auto v = new ext_vdata;
    ext4_fs fs;
    unsigned expected = 0;
    for (unsigned i = 0; i < RA_WORKERS; ++i)
        expected += !(fail_mask & (1u << i));
    assert(ext_ensure_workers(v, &fs, 17) == (expected != 0));
    assert(v->nworkers == expected);
    assert(attempts == RA_WORKERS); // continue attempting after a failure
    unsigned jobs = argc == 4 ? RA_WINDOWS : expected;
    if (argc == 4) assert(expected == 1);
    if (expected) {
        // The queued case fills all slots with just one surviving worker.
        // Every survivor must run real work, including the last attempted slot.
        pthread_mutex_lock(&v->ra_cvmtx);
        for (unsigned i = 0; i < jobs; ++i) {
            v->win_buf[i] = static_cast<char*>(malloc(16));
            assert(v->win_buf[i]);
            v->win_state[i] = RA_FILLING;
            v->jobq[v->jobq_n++] = {i, 0, 16, v->gen};
            ++v->inflight;
        }
        pthread_cond_broadcast(&v->ra_job_cv);
        pthread_mutex_unlock(&v->ra_cvmtx);
        while (entered.load() != expected) std::this_thread::yield();
        assert(!release_read); // read completes only after drain or teardown
        pthread_mutex_lock(&v->ra_cvmtx);
        assert(v->jobq_n == jobs - expected);
        assert(v->inflight == jobs);
        pthread_mutex_unlock(&v->ra_cvmtx);
        assert(ext_ensure_workers(v, &fs, 17));
        assert(attempts == RA_WORKERS); // successful partial pool is reused
    }
    if (argc >= 3) {
        assert(expected);
        mount mp{&fs};
        vnode vp{&mp, 17, v};
        (void)vp;
        drain_cv = &v->ra_done_cv;
        inode_error = atoi(argv[2]);
        bool updated = false;
        int r = TRUNC_CALL;
        assert(r == (inode_error ? EIO : 0));
        assert(completed == jobs);
        assert(v->inflight == 0 && v->jobq_n == 0);
        for (unsigned i = 0; i < RA_WINDOWS; ++i)
            assert(v->win_state[i] == RA_EMPTY);
        assert(!v->ring_active);
        if (!inode_error) assert(inode.size == 0 && updated);
    }
    delete v; // actual production destructor, including cond/buffer destruction
    assert(joined == expected);
    assert(completed == jobs);
    printf("PASS failure mask=%u: %u workers joined, %u reads drained before destruction\n",
           fail_mask, expected, jobs);
}
'''
with tempfile.TemporaryDirectory(prefix='tst-ext4-workers-') as tmp:
    cpp = Path(tmp) / 'workers.cc'
    exe = Path(tmp) / 'workers'
    cpp.write_text(prefix + body + suffix.replace('TRUNC_CALL', trunc_call))
    subprocess.run([os.environ.get('CXX', 'g++'), '-std=c++11', '-Wall', '-Wextra',
                    '-Werror', '-pthread', '-fsanitize=address,undefined',
                    '-fno-omit-frame-pointer', str(cpp), '-o', str(exe)], check=True)
    failed = []
    for mask in range(16):
        p = subprocess.run([str(exe), str(mask)], timeout=15)
        if p.returncode:
            failed.append(mask)
    for error in range(2):
        p = subprocess.run([str(exe), '0', str(error)], timeout=15)
        if p.returncode:
            failed.append(f'truncate error={error}')
        # One running job + three queued slots must all drain on both paths.
        p = subprocess.run([str(exe), '14', str(error), 'queued'], timeout=15)
        if p.returncode:
            failed.append(f'queued truncate error={error}')
    assert not failed, f'Worker lifecycle failed for cases {failed}'
