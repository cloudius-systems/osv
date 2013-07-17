
#include <sys/cdefs.h>

#include "drivers/virtio.hh"
#include "drivers/virtio-blk.hh"
#include "drivers/pci-device.hh"
#include "interrupt.hh"

#include "mempool.hh"
#include "mmu.hh"

#include <sstream>
#include <string>
#include <string.h>
#include <map>
#include <errno.h>
#include <osv/debug.h>

#include "sched.hh"
#include "drivers/clock.hh"
#include "drivers/clockevent.hh"

#include <osv/device.h>
#include <osv/bio.h>

using namespace memory;


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

static int
virtio_blk_read(struct device *dev, struct uio *uio, int ioflags)
{
    struct virtio_blk_priv *prv =
        reinterpret_cast<struct virtio_blk_priv*>(dev->private_data);

    if (uio->uio_offset + uio->uio_resid > prv->drv->size())
        return EIO;

    return bdev_read(dev, uio, ioflags);
}

static int
virtio_blk_write(struct device *dev, struct uio *uio, int ioflags)
{
    struct virtio_blk_priv *prv =
        reinterpret_cast<struct virtio_blk_priv*>(dev->private_data);

    if (prv->drv->is_readonly()) return EROFS;
    if (uio->uio_offset + uio->uio_resid > prv->drv->size())
        return EIO;

    return bdev_write(dev, uio, ioflags);
}

static struct devops virtio_blk_devops {
    no_open,
    no_close,
    virtio_blk_read,
    virtio_blk_write,
    no_ioctl,
    no_devctl,
    virtio_blk_strategy,
};

struct driver virtio_blk_driver = {
    "virtio_blk",
    &virtio_blk_devops,
    sizeof(struct virtio_blk_priv),
};

virtio_blk::virtio_blk(pci::device& pci_dev)
    : virtio_driver(pci_dev), _ro(false)
{
    std::stringstream ss;
    ss << "virtio-blk";

    _driver_name = ss.str();
    _id = _instance++;
    virtio_i("VIRTIO BLK INSTANCE %d", _id);

    read_config();

    //register the single irq callback for the block
    sched::thread* t = new sched::thread([this] { this->response_worker(); });
    t->start();
    _msi.easy_register({ { 0, nullptr, t } });

    add_dev_status(VIRTIO_CONFIG_S_DRIVER_OK);

    struct virtio_blk_priv* prv;
    struct device *dev;
    std::string dev_name("vblk");
    dev_name += std::to_string(_id);

    dev = device_create(&virtio_blk_driver, dev_name.c_str(), D_BLK);
    prv = reinterpret_cast<struct virtio_blk_priv*>(dev->private_data);
    prv->drv = this;
    dev->size = prv->drv->size();
}

virtio_blk::~virtio_blk()
{
    //TODO: In theory maintain the list of free instances and gc it
    // including the thread objects and their stack
}

bool virtio_blk::read_config()
{
    //read all of the block config (including size, mce, topology,..) in one shot
    virtio_conf_read(virtio_pci_config_offset(), &_config, sizeof(_config));

    virtio_i("The capacity of the device is %d", (u64)_config.capacity);
    if (get_guest_feature_bit(VIRTIO_BLK_F_SIZE_MAX))
        virtio_i("The size_max of the device is %d",(u32)_config.size_max);
    if (get_guest_feature_bit(VIRTIO_BLK_F_SEG_MAX))
        virtio_i("The seg_size of the device is %d",(u32)_config.seg_max);
    if (get_guest_feature_bit(VIRTIO_BLK_F_GEOMETRY)) {
        virtio_i("The cylinders count of the device is %d",(u16)_config.geometry.cylinders);
        virtio_i("The heads count of the device is %d",(u32)_config.geometry.heads);
        virtio_i("The sector count of the device is %d",(u32)_config.geometry.sectors);
    }
    if (get_guest_feature_bit(VIRTIO_BLK_F_BLK_SIZE))
        virtio_i("The block size of the device is %d",(u32)_config.blk_size);
    if (get_guest_feature_bit(VIRTIO_BLK_F_TOPOLOGY)) {
        virtio_i("The physical_block_exp of the device is %d",(u32)_config.physical_block_exp);
        virtio_i("The alignment_offset of the device is %d",(u32)_config.alignment_offset);
        virtio_i("The min_io_size of the device is %d",(u16)_config.min_io_size);
        virtio_i("The opt_io_size of the device is %d",(u32)_config.opt_io_size);
    }
    if (get_guest_feature_bit(VIRTIO_BLK_F_CONFIG_WCE))
        virtio_i("The write cache enable of the device is %d",(u32)_config.wce);
    if (get_guest_feature_bit(VIRTIO_BLK_F_RO)) {
        set_readonly();
        virtio_i("Device is read only");
    }

    return true;
}

void virtio_blk::response_worker() {
    vring* queue = get_virt_queue(0);
    virtio_blk_req* req;

    while (1) {

        sched::thread::wait_until([this] {
            vring* queue = get_virt_queue(0);
            return queue->used_ring_not_empty();
        });

        u32 len;
        while((req = static_cast<virtio_blk_req*>(queue->get_buf_elem(&len))) != nullptr) {
            if (req->bio) biodone(req->bio, true);
            req->bio = nullptr;
            delete req;
            queue->get_buf_finalize();
        }

        // wake up the requesting thread in case the ring was full before
        _request_thread_lock.lock();
        if (_waiting_request_thread) {
            _waiting_request_thread->wake();
        }
        _request_thread_lock.unlock();
    }
}

int64_t virtio_blk::size() {
    return _config.capacity * _config.blk_size;
}

static const int page_size = 4096;
static const int sector_size = 512;

int virtio_blk::make_virtio_request(struct bio* bio)
{
    // The lock is here for parallel requests protection
    return with_lock(_lock, [&] {

        if (!bio) return EIO;

        if (bio->bio_bcount/page_size + 1 > _config.seg_max) {
            virtio_w("%s:request of size %d needs more segment than the max %d",
                    __FUNCTION__, bio->bio_bcount, (u32)_config.seg_max);
            return EIO;
        }

        vring* queue = get_virt_queue(0);
        virtio_blk_request_type type;

        switch (bio->bio_cmd) {
        case BIO_READ:
            type = VIRTIO_BLK_T_IN;
            break;
        case BIO_WRITE:
            if (is_readonly()) {
                virtio_e("Error: block device is read only");
                biodone(bio, false);
                return EROFS;
            }
            type = VIRTIO_BLK_T_OUT;
            break;
        default:
            return ENOTBLK;
        }

        virtio_blk_req* req = new virtio_blk_req;
        req->bio = bio;
        virtio_blk_outhdr* hdr = &req->hdr;
        hdr->type = type;
        hdr->ioprio = 0;
        hdr->sector = (int)bio->bio_offset/ sector_size;

        queue->_sg_vec.clear();
        queue->_sg_vec.push_back(vring::sg_node(mmu::virt_to_phys(hdr), sizeof(struct virtio_blk_outhdr), vring_desc::VRING_DESC_F_READ));

        // need to break a contiguous buffers that > 4k into several physical page mapping
        // even if the virtual space is contiguous.
        long len = 0;
        int offset = bio->bio_offset;
        offset = 0xfff & reinterpret_cast<long>(bio->bio_data);
        void *base = bio->bio_data;
        while (len != bio->bio_bcount) {
            long size = std::min(bio->bio_bcount - len, (long)page_size);
            if (offset + size > page_size)
                size = page_size - offset;
            len += size;
            queue->_sg_vec.push_back(vring::sg_node(mmu::virt_to_phys(base), size, (type == VIRTIO_BLK_T_OUT)? vring_desc::VRING_DESC_F_READ:vring_desc::VRING_DESC_F_WRITE));
            base += size;
            offset = 0;
        }

        req->res.status = 0;
        queue->_sg_vec.push_back(vring::sg_node(mmu::virt_to_phys(&req->res), sizeof (struct virtio_blk_res), vring_desc::VRING_DESC_F_WRITE));

        while (!queue->add_buf(req)) {
            _waiting_request_thread = sched::thread::current();
            std::atomic_thread_fence(std::memory_order_seq_cst);
            sched::thread::wait_until([queue] {queue->get_buf_gc(); return queue->avail_ring_has_room(queue->_sg_vec.size());});
            _request_thread_lock.lock();
            _waiting_request_thread = nullptr;
            _request_thread_lock.unlock();
        }

        queue->kick();

        return 0;
    });
}

u32 virtio_blk::get_driver_features(void)
{
    u32 base = virtio_driver::get_driver_features();
    return (base | ( 1 << VIRTIO_BLK_F_SIZE_MAX)
                 | ( 1 << VIRTIO_BLK_F_SEG_MAX)
                 | ( 1 << VIRTIO_BLK_F_GEOMETRY)
                 | ( 1 << VIRTIO_BLK_F_RO)
                 | ( 1 << VIRTIO_BLK_F_BLK_SIZE));
}

hw_driver* virtio_blk::probe(hw_device* dev)
{
    if (auto pci_dev = dynamic_cast<pci::device*>(dev)) {
        if (pci_dev->get_id() == hw_device_id(VIRTIO_VENDOR_ID, VIRTIO_BLK_DEVICE_ID)) {
            return new virtio_blk(*pci_dev);
        }
    }
    return nullptr;
}

}
