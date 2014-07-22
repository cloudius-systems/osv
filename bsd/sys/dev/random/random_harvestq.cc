/*-
 * Copyright (c) 2000-2013 Mark R V Murray
 * Copyright (c) 2013 Arthur Mesh
 * Copyright (c) 2004 Robert N. M. Watson
 * All rights reserved.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions
 * are met:
 * 1. Redistributions of source code must retain the above copyright
 *    notice, this list of conditions and the following disclaimer
 *    in this position and unchanged.
 * 2. Redistributions in binary form must reproduce the above copyright
 *    notice, this list of conditions and the following disclaimer in the
 *    documentation and/or other materials provided with the distribution.
 *
 * THIS SOFTWARE IS PROVIDED BY THE AUTHOR ``AS IS'' AND ANY EXPRESS OR
 * IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE IMPLIED WARRANTIES
 * OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE ARE DISCLAIMED.
 * IN NO EVENT SHALL THE AUTHOR BE LIABLE FOR ANY DIRECT, INDIRECT,
 * INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES (INCLUDING, BUT
 * NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES; LOSS OF USE,
 * DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND ON ANY
 * THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT
 * (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE OF
 * THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
 *
 */

#include <sys/cdefs.h>
__FBSDID("$FreeBSD$");

#include "opt_random.h"

#include <sys/param.h>
#include <sys/systm.h>
#include <sys/eventhandler.h>
#include <sys/kernel.h>
#include <sys/kthread.h>
#ifndef __OSV__
#include <sys/linker.h>
#endif
#include <sys/lock.h>
#include <sys/malloc.h>
#include <sys/mutex.h>
#include <sys/random.h>
#include <sys/selinfo.h>
#include <sys/sysctl.h>
#include <sys/unistd.h>
#ifndef __OSV__
#include <machine/cpu.h>
#endif
#include <machine/vmparam.h>

#include <dev/random/randomdev.h>
#include <dev/random/randomdev_soft.h>
#include <dev/random/random_adaptors.h>
#include <dev/random/random_harvestq.h>
#include <dev/random/live_entropy_sources.h>
#ifndef __OSV__
#include <dev/random/rwfile.h>
#endif

#define RANDOM_FIFO_MAX	1024	/* How many events to queue up */

#ifdef __OSV__
#include <stddef.h>
#include <sys/bus.h>
#include <lockfree/unordered_ring_mpsc.hh>
#endif

/* <0 to end the kthread, 0 to let it run, 1 to flush the harvest queues */
int random_kthread_control = 0;

static struct proc *random_kthread_proc;

#ifdef RANDOM_RWFILE
static const char *entropy_files[] = {
	"/entropy",
	NULL
};
#endif

using ring_t = unordered_ring_mpsc<struct harvest,HARVEST_RING_SIZE>;
ring_t* ring;

#ifndef __OSV__
/* Deal with entropy cached externally if this is present.
 * Lots of policy may eventually arrive in this function.
 * Called after / is mounted.
 */
static void
random_harvestq_cache(void *arg __unused)
{
	uint8_t *keyfile, *data;
	size_t size, i;
#ifdef RANDOM_RWFILE
	const char **entropy_file;
	uint8_t *zbuf;
	int error;
#endif

	/* Get stuff that may have been preloaded by loader(8) */
	keyfile = preload_search_by_type("/boot/entropy");
	if (keyfile != NULL) {
		data = preload_fetch_addr(keyfile);
		size = preload_fetch_size(keyfile);
		if (data != NULL && size != 0) {
			for (i = 0; i < size; i += 16)
				random_harvestq_internal(get_cyclecount(), data + i, 16, 16, RANDOM_CACHED);
			printf("random: read %zu bytes from preloaded cache\n", size);
			bzero(data, size);
		}
		else
			printf("random: no preloaded entropy cache available\n");
	}

#ifdef RANDOM_RWFILE
	/* Read and attempt to overwrite the entropy cache files.
	 * If the file exists, can be read and then overwritten,
	 * then use it. Ignore it otherwise, but print out what is
	 * going on.
	 */
	data = malloc(PAGE_SIZE, M_ENTROPY, M_WAITOK);
	zbuf = __DECONST(void *, zero_region);
	for (entropy_file = entropy_files; *entropy_file; entropy_file++) {
		error = randomdev_read_file(*entropy_file, data, PAGE_SIZE);
		if (error == 0) {
			printf("random: entropy cache '%s' provides %ld bytes\n", *entropy_file, (long)PAGE_SIZE);
			error = randomdev_write_file(*entropy_file, zbuf, PAGE_SIZE);
			if (error == 0) {
				printf("random: entropy cache '%s' contents used and successfully overwritten\n", *entropy_file);
				for (i = 0; i < PAGE_SIZE; i += 16)
					random_harvestq_internal(get_cyclecount(), data + i, 16, 16, RANDOM_CACHED);
			}
			else
				printf("random: entropy cache '%s' not overwritten and therefore not used; error = %d\n", *entropy_file, error);
		}
		else
			printf("random: entropy cache '%s' not present or unreadable; error = %d\n", *entropy_file, error);
	}
	bzero(data, PAGE_SIZE);
	free(data, M_ENTROPY);
#endif
}
EVENTHANDLER_DEFINE(mountroot, random_harvestq_cache, NULL, 0);
#endif

static void
random_kthread(void *arg)
{
	event_proc_f entropy_processor = reinterpret_cast<event_proc_f>(arg);

	/* Process until told to stop */
	for (; random_kthread_control >= 0;) {
		/*
		 * Grab all the entropy events.
		 * Drain entropy source records into a thread-local
		 * queue for processing while not holding the mutex.
		 */

		/*
		 * Deal with events, if any.
		 * Then transfer the used events back into the empty fifo.
		 */
		for (auto& event : ring->drain()) {
			entropy_processor(&event);
		}

		/*
		 * Do only one round of the hardware sources for now.
		 * Later we'll need to make it rate-adaptive.
		 */
		live_entropy_sources_feed(1, entropy_processor);

		/*
		 * If a queue flush was commanded, it has now happened,
		 * and we can mark this by resetting the command.
		 */

		if (random_kthread_control == 1)
			random_kthread_control = 0;

#ifdef __OSV__
		tsleep(&random_kthread_control, 0, "-", hz/10);
#else
		/* Work done, so don't belabour the issue */
		msleep_spin_sbt(&random_kthread_control, &harvest_mtx,
		    "-", SBT_1S/10, 0, C_PREL(1));
#endif

	}

	random_set_wakeup_exit(&random_kthread_control);
	/* NOTREACHED */
}

void
random_harvestq_init(event_proc_f cb)
{
	ring = new ring_t();

	live_entropy_sources_init(NULL);

	/* Start the hash/reseed thread */
	int error = kproc_create(reinterpret_cast<void(*)(void*)>(random_kthread), reinterpret_cast<void*>(cb),
		&random_kthread_proc, RFHIGHPID, 0, "rand_harvestq"); /* RANDOM_CSPRNG_NAME */

	if (error != 0)
		panic("Cannot create entropy maintenance thread.");
}

void
random_harvestq_deinit(void)
{
	delete ring;
	live_entropy_sources_deinit(NULL);
}

/*
 * Entropy harvesting routine.
 * This is supposed to be fast; do not do anything slow in here!
 *
 * It is also illegal (and morally reprehensible) to insert any
 * high-rate data here. "High-rate" is define as a data source
 * that will usually cause lots of failures of the "Lockless read"
 * check a few lines below. This includes the "always-on" sources
 * like the Intel "rdrand" or the VIA Nehamiah "xstore" sources.
 */
void
random_harvestq_internal(u_int64_t somecounter, const void *entropy,
    u_int count, u_int bits, enum esource origin)
{
	KASSERT(origin >= RANDOM_START && origin < ENTROPYSOURCE,
	    ("random_harvest_internal: origin %d invalid\n", origin));

	ring->emplace(somecounter, entropy, count, bits, origin);
}
