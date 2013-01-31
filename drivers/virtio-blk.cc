#include "drivers/virtio.hh"
#include "drivers/virtio-blk.hh"
#include "drivers/pci-device.hh"

#include "mempool.hh"
#include "mmu.hh"
#include "sglist.hh"

#include <sstream>
#include <string>
#include <string.h>
#include "debug.hh"
#include "cdefs.hh"

#include "sched.hh"
#include "drivers/clock.hh"
#include "drivers/clockevent.hh"


using namespace memory;

namespace virtio {

int virtio_blk::_instance = 0;

    virtio_blk::virtio_blk(unsigned dev_idx)
        : virtio_driver(VIRTIO_BLK_DEVICE_ID, dev_idx)
    {
        std::stringstream ss;
        ss << "virtio-blk" << dev_idx;

        _driver_name = ss.str();
        _id = _instance++;
    }

    virtio_blk::~virtio_blk()
    {
        //TODO: In theory maintain the list of free instances and gc it
    }


    bool virtio_blk::load(void)
    {
        virtio_driver::load();
        
        _dev->virtio_conf_read(__offsetof(struct virtio_blk_config, capacity) + VIRTIO_PCI_CONFIG(_dev),
                      &_config.capacity,
                      sizeof(_config.capacity));
        debug(fmt("capacity of the device is %x") % (u64)_config.capacity);

        _dev->add_dev_status(VIRTIO_CONFIG_S_DRIVER_OK);

        // Perform test if this isn't the boot image (test is destructive
        if (_id > 0) {
            debug(fmt("virtio blk: testing instance %d") % _id);
            test();
        }

        return true;
    }

    bool virtio_blk::unload(void)
    {
        return (true);
    }

    virtio_blk::virtio_blk_req* virtio_blk::make_virtio_req(u64 sector, virtio_blk_request_type type, int val)
    {
        sglist* sg = new sglist();
        void* buf = malloc(page_size);
        memset(buf, val, page_size);
        sg->add(mmu::virt_to_phys(buf), page_size);

        virtio_blk_req* req = new virtio_blk_req;
        virtio_blk_outhdr* hdr = new virtio_blk_outhdr;
        hdr->type = type;
        hdr->ioprio = 0;
        hdr->sector = sector;

        //push 'output' buffers to the beginning of the sg list
        sg->add(mmu::virt_to_phys(hdr), sizeof(struct virtio_blk_outhdr), true);

        virtio_blk_res* res = reinterpret_cast<virtio_blk_res*>(malloc(sizeof(virtio_blk_res)));
        res->status = 0;
        sg->add(mmu::virt_to_phys(res), sizeof (struct virtio_blk_res));

        req->status = res;
        req->req_header = hdr;
        req->payload = sg;

        return req;
    }

    void virtio_blk::test() {
        int i;

        debug("test virtio blk");
        vring* queue = _dev->get_virt_queue(0);
        virtio_blk_req* req;
        const int iterations = 10;

        debug(" write several requests");
        for (i=0;i<iterations;i++) {
            req = make_virtio_req(i*8, VIRTIO_BLK_T_OUT,i);
            if (!queue->add_buf(req->payload,2,1,req)) {
                break;
            }
        }

        debug(fmt(" Let the host know about the %d requests") % i);
        queue->kick();

        debug(" Wait for the irq to be injected by sleeping 1 sec");
        timespec ts = {};
        ts.tv_sec = 1;
        nanosleep(&ts, nullptr);

        debug(" Collect the block write responses");
        i = 0;
        while((req = reinterpret_cast<virtio_blk_req*>(queue->get_buf())) != nullptr) {
            debug(fmt("\t got response:%d = %d ") % i++ % (int)req->status->status);

            delete req->status;
            delete reinterpret_cast<virtio_blk_outhdr*>(req->req_header);
            delete req->payload;
        }

        debug(" read several requests");
        for (i=0;i<iterations;i++) {
            req = make_virtio_req(i*8, VIRTIO_BLK_T_IN,0);
            if (!queue->add_buf(req->payload,1,2,req)) {
                break;
            }
        }

        debug(fmt(" Let the host know about the %d requests") % i);
        queue->kick();

        debug(" Wait for the irq to be injected by sleeping 1 sec");
        ts.tv_sec = 1;
        nanosleep(&ts, nullptr);

        debug(" Collect the block read responses");
        i = 0;
        while((req = reinterpret_cast<virtio_blk_req*>(queue->get_buf())) != nullptr) {
            debug(fmt("\t got response:%d = %d ") % i % (int)req->status->status);

            debug(fmt("\t verify that sector %d contains data %d") % (i*8) % i);
            i++;
            auto ii = req->payload->_nodes.begin();
            ii++;
            char*buf = reinterpret_cast<char*>(mmu::phys_to_virt(ii->_paddr));
            debug(fmt("\t value = %d len=%d") % (int)(*buf) % ii->_len);

            delete req->status;
            delete reinterpret_cast<virtio_blk_outhdr*>(req->req_header);
            delete req->payload;
        }


        debug("test virtio blk end");
    }

}

