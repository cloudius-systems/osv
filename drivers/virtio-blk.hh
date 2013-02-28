#ifndef VIRTIO_BLK_DRIVER_H
#define VIRTIO_BLK_DRIVER_H

/* This header is BSD licensed so anyone can use the definitions to implement
 * compatible drivers/servers.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions
 * are met:
 * 1. Redistributions of source code must retain the above copyright
 *    notice, this list of conditions and the following disclaimer.
 * 2. Redistributions in binary form must reproduce the above copyright
 *    notice, this list of conditions and the following disclaimer in the
 *    documentation and/or other materials provided with the distribution.
 * 3. Neither the name of IBM nor the names of its contributors
 *    may be used to endorse or promote products derived from this software
 *    without specific prior written permission.
 * THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS ``AS IS'' AND
 * ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE
 * IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE
 * ARE DISCLAIMED.  IN NO EVENT SHALL IBM OR CONTRIBUTORS BE LIABLE
 * FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL
 * DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS
 * OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION)
 * HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT
 * LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY
 * OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF
 * SUCH DAMAGE. */


#include "drivers/virtio.hh"
#include "drivers/pci-device.hh"
#include <osv/bio.h>


namespace virtio {

    class virtio_blk : public virtio_driver {
    public:

        // The feature bitmap for virtio blk
        enum BlkFeatures {
            VIRTIO_BLK_F_BARRIER=0,        /* Does host support barriers? */
            VIRTIO_BLK_F_SIZE_MAX=1,       /* Indicates maximum segment size */
            VIRTIO_BLK_F_SEG_MAX=2,        /* Indicates maximum # of segments */
            VIRTIO_BLK_F_GEOMETRY=4,       /* Legacy geometry available  */
            VIRTIO_BLK_F_RO=5,             /* Disk is read-only */
            VIRTIO_BLK_F_BLK_SIZE=6,       /* Block size of disk is available*/
            VIRTIO_BLK_F_SCSI=7,           /* Supports scsi command passthru */
            VIRTIO_BLK_F_WCE=9,            /* Writeback mode enabled after reset */
            VIRTIO_BLK_F_TOPOLOGY=10,      /* Topology information is available */
            VIRTIO_BLK_F_CONFIG_WCE=11,    /* Writeback mode available in config */
        };

        enum {
            VIRTIO_BLK_DEVICE_ID=0x1001,
            VIRTIO_BLK_ID_BYTES=20,      /* ID string length */

            /*
             * Command types
             *
             * Usage is a bit tricky as some bits are used as flags and some are not.
             *
             * Rules:
             *   VIRTIO_BLK_T_OUT may be combined with VIRTIO_BLK_T_SCSI_CMD or
             *   VIRTIO_BLK_T_BARRIER.  VIRTIO_BLK_T_FLUSH is a command of its own
             *   and may not be combined with any of the other flags.
             */
        };

        enum virtio_blk_request_type {
            VIRTIO_BLK_T_IN=0,
            VIRTIO_BLK_T_OUT=1,
            /* This bit says it's a scsi command, not an actual read or write. */
            VIRTIO_BLK_T_SCSI_CMD=2,
            /* Cache flush command */
            VIRTIO_BLK_T_FLUSH=4,
            /* Get device ID command */
            VIRTIO_BLK_T_GET_ID=8,
            /* Barrier before this op. */
            VIRTIO_BLK_T_BARRIER=0x80000000,
        };

        enum virtio_blk_res_code {
            /* And this is the final byte of the write scatter-gather list. */
            VIRTIO_BLK_S_OK=0,
            VIRTIO_BLK_S_IOERR=1,
            VIRTIO_BLK_S_UNSUPP=2,
        };

        struct virtio_blk_config {
                /* The capacity (in 512-byte sectors). */
                u64 capacity;
                /* The maximum segment size (if VIRTIO_BLK_F_SIZE_MAX) */
                u32 size_max;
                /* The maximum number of segments (if VIRTIO_BLK_F_SEG_MAX) */
                u32 seg_max;
                /* geometry the device (if VIRTIO_BLK_F_GEOMETRY) */
                struct virtio_blk_geometry {
                        u16 cylinders;
                        u8 heads;
                        u8 sectors;
                } geometry;

                /* block size of device (if VIRTIO_BLK_F_BLK_SIZE) */
                u32 blk_size;

                /* the next 4 entries are guarded by VIRTIO_BLK_F_TOPOLOGY  */
                /* exponent for physical block per logical block. */
                u8 physical_block_exp;
                /* alignment offset in logical blocks. */
                u8 alignment_offset;
                /* minimum I/O size without performance penalty in logical blocks. */
                u16 min_io_size;
                /* optimal sustained I/O size in logical blocks. */
                u32 opt_io_size;

                /* writeback mode (if VIRTIO_BLK_F_CONFIG_WCE) */
                u8 wce;
        } __attribute__((packed));

        /* This is the first element of the read scatter-gather list. */
        struct virtio_blk_outhdr {
                /* VIRTIO_BLK_T* */
                u32 type;
                /* io priority. */
                u32 ioprio;
                /* Sector (ie. 512 byte offset) */
                u64 sector;
        };

        struct virtio_scsi_inhdr {
                u32 errors;
                u32 data_len;
                u32 sense_len;
                u32 residual;
        };
        
        struct virtio_blk_res {
            u8 status;
        };

        struct virtio_blk_req {
            virtio_blk_req(void* req = nullptr, sglist* sg = nullptr, virtio_blk_res* res = nullptr, struct bio* b=nullptr)
                :req_header(req), payload(sg), status(res), bio(b) {};
            ~virtio_blk_req();
            void* req_header;
            sglist* payload;
            virtio_blk_res* status;
            struct bio* bio;
        };

        explicit virtio_blk(virtio_device* dev);
        virtual ~virtio_blk();

        virtual const std::string get_name(void) { return _driver_name; }
        virtual bool load(void);
        virtual bool unload(void);
        bool read_config();

        virtual u32 get_driver_features(void);

        int make_virtio_request(struct bio*);

        void response_worker();
        int size();

        void set_readonly() {_ro = true;}
        bool is_readonly() {return _ro;}

        static hw_driver* probe(hw_device* dev);
    private:

        std::string _driver_name;
        virtio_blk_config _config;

        //maintains the virtio instance number for multiple drives
        static int _instance;
        int _id;
        bool _ro;
    };

}

#endif

