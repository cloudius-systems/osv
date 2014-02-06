/*
 * Copyright (C) 2014 Cloudius Systems, Ltd.
 *
 * This work is open source software, licensed under the terms of the
 * BSD license as described in the LICENSE file in the top-level directory.
 */

#include <osv/mutex.h>
#include <osv/mempool.hh>
#include <osv/run.hh>
#include <osv/debug.hh>

#include "stat.hh"
#include <assert.h>
#include <fcntl.h>
#include <stdio.h>
#include <unistd.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <sys/param.h>
#include <sys/mman.h>
#include <boost/program_options.hpp>
#include <chrono>
#include <unordered_map>

/* .../sys/kstat.h dependencies */
typedef u_char uchar_t;
typedef u_long ulong_t;

#include <bsd/sys/cddl/compat/opensolaris/sys/kstat.h>
#include <bsd/machine/atomic.h>
#include <bsd/porting/netport.h>

#define MB (1024 * 1024)

using namespace std;
namespace po = boost::program_options;
static std::chrono::high_resolution_clock s_clock;

extern "C" {
    uint64_t kmem_size(void);
    uint64_t kmem_used(void);
    void arc_shrink(void);
}
extern kstat_t *arc_ksp; /* import ZFS ARC stats */

/* Comparison purposes, e.g. old against new state */
struct arc_data {
    uint64_t hits;
    uint64_t misses;
    uint64_t target;
    uint64_t size;
};

static mutex_t kstat_map_mutex;
static unordered_map<const char *, struct kstat_named *> kstat_map;

static struct kstat_named *kstat_map_lookup(const char *name)
{
    auto knp = kstat_map.find(name);
    if (knp == kstat_map.end()) {
        return NULL;
    }
    return (*knp).second;
}

static struct kstat_named *
kstat_map_insert(const kstat_t *ksp, const char *name)
{
    struct kstat_named *knp;

    for (unsigned i = 0; i <= ksp->ks_ndata; i++) {
        knp = &(((struct kstat_named *) ksp->ks_data)[i]);

        if (!strncmp(knp->name, name, 31)) {
            kstat_map.insert(make_pair(name, knp));
            return knp;
        }
    }
    return NULL;
}

static uint64_t *get_kstat_by_name(const kstat_t *ksp, const char *name)
{
    struct kstat_named *knp;

    assert(ksp && ksp->ks_data);

    WITH_LOCK(kstat_map_mutex) {
        knp = kstat_map_lookup(name);

        /* If knp is NULL, kstat_named wasn't found in the hash */
        if (!knp) {
            /* Then do the manual search and insert it into the hash */
            knp = kstat_map_insert(ksp, name);
            if (!knp) {
                return 0;
            }
        }
    }
    assert(knp->data_type == KSTAT_DATA_UINT64);

    return &(knp->value.ui64);
}

static uint64_t get_arc_stat(const kstat_t *ksp, const char *name)
{
    uint64_t *stat_ptr = get_kstat_by_name(ksp, name);
    return *stat_ptr;
}

static void set_arc_stat(const kstat_t *ksp, const char *name, uint64_t value)
{
    uint64_t *stat_ptr = get_kstat_by_name(ksp, name);
    *stat_ptr = value;
}

/*
 * calculate arc_c_max (ARC target max).
 * Reproduce calculus in the ARC initialization code that sets up arc_c_max.
 */
static uint64_t get_arc_c_max(void)
{
    uint64_t arc_c, arc_c_min, arc_c_max;
    uint64_t mem_size;

    mem_size = kmem_size();

    /* Set the target size of ARC to 1/8 of all the memory */
    arc_c = mem_size / 8;
    /* Set arc_c_min to the highest value (1/32 of all the memory or 16MB) */
    arc_c_min = MAX(mem_size / 32, 16*MB);

    /* Set arc_c_max to all the memory but 1GB if such a memory is available,
     * otherwise set it to arc_c_min */
    if (mem_size >= 1<<30) {
        arc_c_max = mem_size - (1<<30);
    } else {
        arc_c_max = arc_c_min;
    }

    /* Keep the previous arc_c_max only if it's higher than 0.625 (5/8) of
     * all the memory */
    arc_c_max = MAX(arc_c * 5, arc_c_max);

    return arc_c_max;
}

static int check_kstat_validity(const kstat_t *ksp)
{
    if (!ksp || !ksp->ks_data || !ksp->ks_ndata) {
        return -1;
    }

    /* Check that the predictable arc_c_max is the same as the one
     * caught from kstat */
    if (get_arc_c_max() != get_arc_stat(arc_ksp, "c_max")) {
        return -1;
    }

    return 0;
}

/*
 * Default shift value used by ARC.
 * Used to calculate the portion of the ARC to reclaim.
 */
constexpr int arc_shrink_shift = 5;

static int check_arc_shrink(const kstat_t *ksp)
{
    uint64_t arc_size, arc_c_min, arc_c;
    uint64_t to_free;
    int ret = 0;

    arc_c = get_arc_stat(ksp, "c");
    arc_c_min = get_arc_stat(ksp, "c_min");
    arc_size = get_arc_stat(ksp, "size");

    /* Reclaiming wouldn't be possible if arc target is lower or equal than
     * arc_c_min. NOTE: If *lower* than, then probably something have gone
     * wrong in the ARC internals */
    if (arc_c <= arc_c_min) {
        fprintf(stderr, "Warning: ARC target (%lu) is lower or equal than the "
            "ARC min target (%lu).\n", arc_c, arc_c_min);
        return -1;
    }

    /* Portion of the ARC to reclaim */
    to_free = arc_c >> arc_shrink_shift;

    /* Check if reclaiming 'to_free' wouldn't set arc_c to a value lower than
     * arc_c_min, if so, set arc_c to arc_c_min */
    if (arc_c > arc_c_min + to_free) {
        arc_c -= to_free;
    } else {
        arc_c = arc_c_min;
    }

    /* Adjust the new target:
     * - Check if the ARC target is higher than the current ARC size.
     * - If so, set ARC target to the max of arc_size and arc_c_min.
     */
    if (arc_c > arc_size) {
        arc_c = MAX(arc_size, arc_c_min);
    }

    /* Call function from the ARC itself to do the shrinking */
    arc_shrink();

    /* Check if the ARC target is the same as the expected one */
    if (get_arc_stat(ksp, "c") != arc_c) {
        ret = -1;
    }

    /* Check if the ARC shrank as expected */
    if (get_arc_stat(ksp, "size") > arc_c) {
        ret = -1;
    }

    return ret;
}

static void zfs_arc_statistics(const kstat_t *ksp)
{
    uint64_t arc_size = get_arc_stat(ksp, "size");
    uint64_t arc_target_size = get_arc_stat(ksp, "c");
    uint64_t arc_min_size = get_arc_stat(ksp, "c_min");
    uint64_t arc_max_size = get_arc_stat(ksp, "c_max");

    assert(arc_size > 0);
    assert(arc_target_size > 0);
    assert(arc_target_size >= arc_min_size && arc_target_size <= arc_max_size);

    /* Cache size breakdown */
    uint64_t arc_mru_size = get_arc_stat(ksp, "p");
    uint64_t arc_mfu_size = arc_target_size - arc_mru_size;
    float arc_mru_perc = 100 * (static_cast<float>(arc_mru_size) / arc_target_size);
    float arc_mfu_perc = 100 * (static_cast<float>(arc_mfu_size) / arc_target_size);

    printf(":: ARC STATS ::\n");
    printf("ARC: actual size:     %lu (%luMB)\n", arc_size, arc_size / MB);
    printf("\tMost recently used (MRU) size:   %luMB (%.2f%%)\n",
           arc_mru_size / MB, arc_mru_perc);
    printf("\tMost frequently used (MFU) size: %luMB (%.2f%%)\n",
           arc_mfu_size / MB, arc_mfu_perc);
    printf("ARC: target size:     %luMB\n", arc_target_size / MB);
    printf("ARC: min target size: %luMB\n", arc_min_size / MB);
    printf("ARC: max target size: %luMB\n", arc_max_size / MB);

    uint64_t arc_hits = get_arc_stat(ksp, "hits");
    uint64_t arc_misses = get_arc_stat(ksp, "misses");
    uint64_t arc_ac_total = arc_hits + arc_misses;

    assert(arc_ac_total > 0);

    float arc_hits_perc = 100 * (static_cast<float>(arc_hits) / arc_ac_total);
    float arc_misses_perc = 100 * (static_cast<float>(arc_misses) / arc_ac_total);

    printf("ARC: total accesses:  %lu\n", arc_ac_total);
    printf("\tCache hits:   %lu (%.2f%%)\n", arc_hits, arc_hits_perc);
    printf("\tCache misses: %lu (%.2f%%)\n", arc_misses, arc_misses_perc);
}

static void create_arc_data(const kstat_t *ksp, struct arc_data &data)
{
    data.hits = get_arc_stat(ksp, "hits");
    data.misses = get_arc_stat(ksp, "misses");
    data.target = get_arc_stat(ksp, "c");
    data.size = get_arc_stat(ksp, "size");
}

static void report_arc_data(const kstat_t *ksp, struct arc_data &data)
{
    uint64_t hits = get_arc_stat(ksp, "hits");
    uint64_t misses = get_arc_stat(ksp, "misses");
    uint64_t target = get_arc_stat(ksp, "c");
    uint64_t size = get_arc_stat(ksp, "size");

    uint64_t actual_hits = hits - data.hits;
    uint64_t actual_misses = misses - data.misses;
    uint64_t total = actual_hits + actual_misses;
    assert(total > 0);
    float cache_hit_ratio = 100 * (static_cast<float>(actual_hits) / total);

    printf(":: ARC REPORT ::\n");
    printf("\thits += %lu\n\tmisses += %lu\n\ttarget += %ld bytes\n\t"
        "size += %ld bytes\n\t* cache hit ratio: %.2f%%\n",
        hits - data.hits, misses - data.misses,
        static_cast<long>(target - data.target),
        static_cast<long>(size - data.size), cache_hit_ratio);
}

#define TESTDIR         "/tests"
static int run_test(const kstat_t *ksp, int argc, char **argv)
{
    struct arc_data data;
    struct stat st;
    char path[PATH_MAX];
    int ret;

    snprintf(path, PATH_MAX, "%s/%s", TESTDIR, argv[0]);
    printf("Running %s", path);
    for (int i = 1; i < argc; i++) {
        printf(" %s", argv[i]);
    }
    printf("...\n");

    if (stat(path, &st) < 0) {
        fprintf(stderr, "failed to stat %s\n", path);
        return -1;
    }

    if (!S_ISREG(st.st_mode)) {
        fprintf(stderr, "ignoring %s, not a regular file\n", path);
        return -1;
    }

    create_arc_data(ksp, data);

    if (!osv::run(path, argc, argv, &ret)) {
        abort("Failed to execute or missing main()\n");
    }

    report_arc_data(ksp, data);

    return ret;
}

static void memory_pressure_scenario(const kstat_t *ksp)
{
    unsigned allocated;
    uint64_t arc_size;
    uint64_t arc_min_target = get_arc_stat(ksp, "c_min");
    uint64_t old_arc_size = get_arc_stat(ksp, "size");

    allocated = 0;
    do {
        mmap(NULL, 64*MB, PROT_READ|PROT_WRITE,
             MAP_PRIVATE|MAP_POPULATE|MAP_ANONYMOUS, -1, 0);
        arc_size = get_arc_stat(ksp, "size");
        allocated += 64U;

        printf("\t* Allocated %04uMB after shrinking ARC size by %04luMB.\n",
            allocated, (old_arc_size - arc_size) / MB);

        if (static_cast<long>(arc_size) - 64*MB < 0) {
            break;
        }
    } while ((arc_size - 64*MB) > arc_min_target);

    zfs_arc_statistics(ksp);
}

/*
 * Test used to check performance on linear workloads.
 */
static int arc_linear_test(const kstat_t *ksp, bool all_cached)
{
    char *args[3] = { 0 };
    int argc, ret = 0;

    args[0] = strdup("misc-zfs-io.so");
    args[1] = strdup("--no-unlink");
    argc = 2;
    if (all_cached) {
        args[2] = strdup("--all-cached");
        argc = 3;
    }
    ret = run_test(ksp, argc, args);

    free(args[0]);
    free(args[1]);
    free(args[2]);

    args[0] = strdup("misc-zfs-io.so");
    args[1] = strdup("--rdonly");
    argc = 2;
    if (all_cached) {
        args[2] = strdup("--all-cached");
        argc = 3;
    }
    ret = run_test(ksp, argc, args);

    free(args[0]);
    free(args[1]);
    free(args[2]);

    zfs_arc_statistics(ksp);

    return ret;
}

/*
 * Test used to check performance on non-linear workloads.
 */
static int arc_nonlinear_test(const kstat_t *ksp)
{
    char *args[2];
    int ret = 0;

    args[0] = strdup("misc-zfs-io.so");
    args[1] = strdup("--random");
    ret = run_test(ksp, 2, args);
    free(args[0]);
    free(args[1]);

    zfs_arc_statistics(ksp);

    return ret;
}

int main(int argc, char **argv)
{
    auto start_time = s_clock.now();
    kstat_t *arc_kstat_p = arc_ksp;
    int ret = 0;

    // Declare the supported options.
    po::options_description desc("Allowed options");
    desc.add_options()
        ("help",
            "produce help message")
        ("set-max-target",
            "set ARC max target to 80% of the system memory.")
        ("check-arc-shrink",
            "check ARC shrink functionality")
        ("test", po::value<std::string>(),
            "analyze ARC performance on a given testcase, e.g. --test tst-001.so");

    po::variables_map vm;
    po::store(po::parse_command_line(argc, argv, desc), vm);
    po::notify(vm);

    if (vm.count("help")) {
        cerr << desc << "\n";
        return -1;
    }

    printf("Starting ZFS ARC analysis...\n");

    if (check_kstat_validity(arc_kstat_p) == -1) {
        fprintf(stderr, "Error: The structure of ARC kstat (arc_ksp) is not valid!\n");
        return -1;
    }

    printf("System Memory:        %luMB\n", kmem_size() / MB);
    printf("\tFree: %luMB\n", memory::stats::free() / MB);
    printf("\tUsed: %luMB\n", kmem_used() / MB);
    zfs_arc_statistics(arc_kstat_p);

    /* Set arc max target to 80% of system memory.
     * NOTE: this change will affect all subsequent tests on the same OSv
     * instance.
     */
    if (vm.count("set-max-target")) {
        unsigned new_arc_target = kmem_size() * 80U / 100U;
        set_arc_stat(arc_kstat_p, "c_max", new_arc_target);
        printf("Setting ARC max target to %dMB (80%% of the system memory)\n",
            new_arc_target / MB);
    }

    if (vm.count("check-arc-shrink")) {
        ret = check_arc_shrink(arc_kstat_p);
        printf("Result: ARC shrink %s.\n", ret == 0 ? "worked" : "didn't work");
    } else if (vm.count("test")) {
        char *arg0 = strdup(vm["test"].as<std::string>().c_str());
        ret = run_test(arc_kstat_p, 1, &arg0);
        free(arg0);
    } else {
        printf("\n*** NON-LINEAR WORKLOAD; PREFETCH SHOULDN'T BE EFFECTIVE ***\n");
        ret = arc_nonlinear_test(arc_kstat_p);
        printf("\n*** CHECK ARC PERFORMANCE WHEN DATA IS ALL CACHED ***\n");
        ret |= arc_linear_test(arc_kstat_p, true);
        printf("\n*** READAHEAD AND PAGE REPLACEMENT SCENARIO ***\n");
        ret |= arc_linear_test(arc_kstat_p, false);
        printf("\n*** MEMORY PRESSURE SCENARIO ***\n");
        memory_pressure_scenario(arc_kstat_p);
    }

    printf("Finishing ZFS ARC analysis...\n");
    auto end_time = s_clock.now();
    auto duration = to_seconds(end_time - start_time);
    printf("ARC analysis was made in %.2f seconds\n", duration);

    return ret;
}
