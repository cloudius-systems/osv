/*
 * Copyright (C) 2013 Cloudius Systems, Ltd.
 *
 * This work is open source software, licensed under the terms of the
 * BSD license as described in the LICENSE file in the top-level directory.
 */

// Simplified implementation of BSD's bus_dma interfaces
#include <bsd/porting/netport.h>
#include <bsd/porting/bus.h>
#include <bsd/porting/mmu.h>
#include <osv/align.hh>

struct bus_dma_tag {
	bus_size_t	  alignment;
	bus_size_t	  maxsize;
	u_int		  nsegments;
	bus_size_t	  maxsegsz;
	int		  map_count;
	bus_dma_lock_t	 *lockfunc;
	void		 *lockfuncarg;
	bus_dma_segment_t *segments;
};

void
busdma_lock_mutex(void *arg, bus_dma_lock_op_t op)
{
	struct mtx *dmtx;

	dmtx = (struct mtx *)arg;
	switch (op) {
	case BUS_DMA_LOCK:
		mtx_lock(dmtx);
		break;
	case BUS_DMA_UNLOCK:
		mtx_unlock(dmtx);
		break;
	default:
		panic("Unknown operation 0x%x for busdma_lock_mutex!", op);
	}
}

int
bus_dma_tag_create(bus_dma_tag_t parent, bus_size_t alignment,
		   bus_addr_t boundary, bus_addr_t lowaddr,
		   bus_addr_t highaddr, bus_dma_filter_t *filter,
		   void *filterarg, bus_size_t maxsize, int nsegments,
		   bus_size_t maxsegsz, int flags, bus_dma_lock_t *lockfunc,
		   void *lockfuncarg, bus_dma_tag_t *dmat)
{
    assert(lockfunc != NULL);

	bus_dma_tag_t newtag = new struct bus_dma_tag;
    if (!newtag)
        return -ENOMEM;

	newtag->alignment = alignment;
	newtag->maxsize = maxsize;
	newtag->nsegments = nsegments;
	newtag->maxsegsz = maxsegsz;
	newtag->map_count = 0;
	newtag->lockfunc = lockfunc;
	newtag->lockfuncarg = lockfuncarg;
	newtag->segments = NULL;

    *dmat = newtag;
    return 0;
}

int
bus_dma_tag_destroy(bus_dma_tag_t dmat)
{
	if (dmat != NULL) {
		if (dmat->map_count != 0) {
			return EBUSY;
		}
        free(dmat);
    }

    return 0;
}

/*
 * Allocate a handle for mapping from kva/uva/physical
 * address space into bus device space.
 */
int
bus_dmamap_create(bus_dma_tag_t dmat, int flags, bus_dmamap_t *mapp)
{
	if (dmat->segments == NULL) {
		dmat->segments = new struct bus_dma_segment[dmat->nsegments];
		if (dmat->segments == NULL) {
			return ENOMEM;
		}
	}

    *mapp = NULL; 
    dmat->map_count++;
    return 0;
}

int
bus_dmamap_destroy(bus_dma_tag_t dmat, bus_dmamap_t map)
{
	dmat->map_count--;
    return 0;
}


/*
 * Map the buffer buf into bus space using the dmamap map.
 */
int
bus_dmamap_load(bus_dma_tag_t dmat, bus_dmamap_t map, void *buf,
		bus_size_t buflen, bus_dmamap_callback_t *callback,
		void *callback_arg, int flags)
{
    unsigned int nsegs = 0;

    while (buflen > 0) {
        auto segsize = std::min(buflen, dmat->maxsegsz);
        segsize = align_up(segsize, dmat->alignment);

        dmat->segments[nsegs].ds_addr = virt_to_phys(buf);
        dmat->segments[nsegs++].ds_len = segsize;
        buf += segsize;
        buflen -= segsize;
    }
    assert(nsegs <= dmat->nsegments);

	(*callback)(callback_arg, dmat->segments, nsegs, 0); 
    return 0;
}

void
_bus_dmamap_unload(bus_dma_tag_t dmat, bus_dmamap_t map)
{
    // Nothing to do here
}

void
_bus_dmamap_sync(bus_dma_tag_t dmat, bus_dmamap_t map, bus_dmasync_op_t op)
{
    // XXX: We don't seem to be using bounce pages. So why does blkfront call this?
}
