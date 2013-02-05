
#include <sys/cdefs.h>

#include "drivers/virtio.hh"
#include "drivers/virtio-blk.hh"
#include "drivers/pci-device.hh"
#include "interrupt.hh"

#include "mempool.hh"
#include "mmu.hh"
#include "sglist.hh"

#include <sstream>
#include <string>
#include <string.h>
#include <map>
#include <errno.h>
#include "debug.hh"

#include "sched.hh"
#include "drivers/clock.hh"
#include "drivers/clockevent.hh"

#include <osv/device.h>
#include <osv/bio.h>

using namespace memory;
using sched::thread;


namespace virtio {

int virtio_blk::_instance = 0;


struct virtio_blk_priv {
    virtio_blk* drv;
};

static void
virtio_blk_strategy(struct bio *bio)
{
    struct virtio_blk_priv *prv = reinterpret_cast<struct virtio_blk_priv*>(bio->bio_dev->private_data);

    prv->drv->make_virtio_request(bio);
}

static struct devops virtio_blk_devops = {
    .open       = no_open,
    .close      = no_close,
    .read       = no_read,
    .write      = no_write,
    .ioctl      = no_ioctl,
    .devctl     = no_devctl,
    .strategy   = virtio_blk_strategy,
};

struct driver virtio_blk_driver = {
    .name       = "virtio_blk",
    .devops     = &virtio_blk_devops,
    .devsz      = sizeof(struct virtio_blk_priv),
};

    virtio_blk::virtio_blk(unsigned dev_idx)
        : virtio_driver(VIRTIO_BLK_DEVICE_ID, dev_idx)
    {
        std::stringstream ss;
        ss << "virtio-blk" << dev_idx;

        _driver_name = ss.str();
        debug(fmt("VIRTIO BLK INSTANCE %d") % dev_idx);
        _id = _instance++;
    }

    virtio_blk::~virtio_blk()
    {
        //TODO: In theory maintain the list of free instances and gc it
    }

    bool virtio_blk::load(void)
    {
        virtio_driver::load();
        
        _dev->virtio_conf_read(offsetof(struct virtio_blk_config, capacity) + VIRTIO_PCI_CONFIG(_dev),
                      &_config.capacity,
                      sizeof(_config.capacity));
        debug(fmt("capacity of the device is %x") % (u64)_config.capacity);

        void* stk1 = malloc(10000);
        thread* worker = new thread([this] { this->response_worker(); } , {stk1, 10000});
        worker->wake(); // just to keep gcc happy about unused var

        _dev->add_dev_status(VIRTIO_CONFIG_S_DRIVER_OK);

        _dev->register_callback([this] { this->response_worker();});

        // Perform test if this isn't the boot image (test is destructive
        if (_id > 0) {
            debug(fmt("virtio blk: testing instance %d") % _id);

            struct virtio_blk_priv* prv;
            struct device *dev;

            dev = device_create(&virtio_blk_driver, "vblk0", D_BLK);
            prv = reinterpret_cast<struct virtio_blk_priv*>(dev->private_data);
            prv->drv = this;

            test();
        }

        return true;
    }

    bool virtio_blk::unload(void)
    {
        return (true);
    }

    // to be removed soon once we move the test from here to the vfs layer
    virtio_blk::virtio_blk_req* virtio_blk::make_virtio_req(u64 sector, virtio_blk_request_type type, int val)
    {
        sglist* sg = new sglist();
        void* buf = malloc(page_size);
        memset(buf, val, page_size);
        sg->add(mmu::virt_to_phys(buf), page_size);

        virtio_blk_outhdr* hdr = new virtio_blk_outhdr;
        hdr->type = type;
        hdr->ioprio = 0;
        hdr->sector = sector;

        //push 'output' buffers to the beginning of the sg list
        sg->add(mmu::virt_to_phys(hdr), sizeof(struct virtio_blk_outhdr), true);

        virtio_blk_res* res = reinterpret_cast<virtio_blk_res*>(malloc(sizeof(virtio_blk_res)));
        res->status = 0;
        sg->add(mmu::virt_to_phys(res), sizeof (struct virtio_blk_res));

        virtio_blk_req* req = new virtio_blk_req(hdr, sg, res);
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

        sched::thread::current()->yield();

        debug(" read several requests");
        for (i=0;i<iterations;i++) {
            req = make_virtio_req(i*8, VIRTIO_BLK_T_IN,0);
            if (!queue->add_buf(req->payload,1,2,req)) {
                break;
            }
            queue->kick(); // should be out of the loop but I like plenty of irqs for the test

        }

        debug(fmt(" Let the host know about the %d requests") % i);
        queue->kick();

        debug("test virtio blk end");
    }

    void virtio_blk::response_worker() {
        vring* queue = _dev->get_virt_queue(0);
        virtio_blk_req* req;

        while (1) {

            debug("\t ----> virtio_blk: response worker main loop");

            thread::wait_until([this] {
                vring* queue = this->_dev->get_virt_queue(0);
                return queue->used_ring_not_empy();
            });

            debug("\t ----> debug - blk thread awaken");

            int i = 0;

            while((req = reinterpret_cast<virtio_blk_req*>(queue->get_buf())) != nullptr) {
                debug(fmt("\t got response:%d = %d ") % i++ % (int)req->status->status);

                virtio_blk_outhdr* header = reinterpret_cast<virtio_blk_outhdr*>(req->req_header);
                //  This is debug code to verify the read content, to be remove later on
                if (header->type == VIRTIO_BLK_T_IN) {
                    debug(fmt("\t verify that sector %d contains data %d") % (int)header->sector % (int)(header->sector/8));
                    i++;
                    auto ii = req->payload->_nodes.begin();
                    ii++;
                    char*buf = reinterpret_cast<char*>(mmu::phys_to_virt(ii->_paddr));
                    debug(fmt("\t value = %d len=%d") % (int)(*buf) % ii->_len);

                }
                if (req->bio != nullptr) {
                    biodone(req->bio);
                    req->bio = nullptr;
                }

                delete req;
            }

        }

    }

    virtio_blk::virtio_blk_req::~virtio_blk_req()
    {
        if (req_header) delete reinterpret_cast<virtio_blk_outhdr*>(req_header);
        if (payload) delete payload;
        if (status) delete status;
        if (bio) delete bio;
    }


    //todo: get it from the host
    int virtio_blk::size() {
        return 1024 * 1024 * 1024;
    }

    static const int page_size = 4096;
    static const int sector_size = 512;

    int virtio_blk::make_virtio_request(struct bio* bio)
        {
            if (!bio) return EIO;

            if ((bio->bio_cmd & (BIO_READ | BIO_WRITE)) == 0)
                return ENOTBLK;

            virtio_blk_request_type type = (bio->bio_cmd == BIO_READ)? VIRTIO_BLK_T_IN:VIRTIO_BLK_T_OUT;
            sglist* sg = new sglist();

            // need to break a contiguous buffers that > 4k into several physical page mapping
            // even if the virtual space is contiguous.
            int len = 0;
            int offset = bio->bio_offset;
            while (len != bio->bio_bcount) {
                int size = std::min((int)bio->bio_bcount - len, page_size);
                if (offset + size > page_size)
                    size = page_size - offset;
                len += size;
                sg->add(mmu::virt_to_phys(bio->bio_data + offset), size);
                offset += size;
            }

            virtio_blk_outhdr* hdr = new virtio_blk_outhdr;
            hdr->type = type;
            hdr->ioprio = 0;
            hdr->sector = (int)bio->bio_offset / sector_size; //wait, isn't offset starts on page addr??

            //push 'output' buffers to the beginning of the sg list
            sg->add(mmu::virt_to_phys(hdr), sizeof(struct virtio_blk_outhdr), true);

            virtio_blk_res* res = reinterpret_cast<virtio_blk_res*>(malloc(sizeof(virtio_blk_res)));
            res->status = 0;
            sg->add(mmu::virt_to_phys(res), sizeof (struct virtio_blk_res));

            virtio_blk_req* req = new virtio_blk_req(hdr, sg, res, bio);
            vring* queue = _dev->get_virt_queue(0);
            int in = 1 , out = 1;
            if (bio->bio_cmd == BIO_READ)
                in += len/page_size + 1;
            else
                out += len/page_size + 1;
            if (!queue->add_buf(req->payload,out,in,req)) {
               //todo: free req;
               return EBUSY;
            }

            queue->kick(); // should be out of the loop but I like plenty of irqs for the test

            return 0;
        }

}
