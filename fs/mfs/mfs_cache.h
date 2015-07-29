/*
 * Copyright 2015 Carnegie Mellon University
 * This material is based upon work funded and supported by the Department of
 * Defense under Contract No. FA8721-05-C-0003 with Carnegie Mellon University
 * for the operation of the Software Engineering Institute, a federally funded
 * research and development center.
 * 
 * Any opinions, findings and conclusions or recommendations expressed in this
 * material are those of the author(s) and do not necessarily reflect the views
 * of the United States Department of Defense.
 * 
 * NO WARRANTY. THIS CARNEGIE MELLON UNIVERSITY AND SOFTWARE ENGINEERING
 * INSTITUTE MATERIAL IS FURNISHED ON AN "AS-IS" BASIS. CARNEGIE MELLON
 * UNIVERSITY MAKES NO WARRANTIES OF ANY KIND, EITHER EXPRESSED OR IMPLIED, AS
 * TO ANY MATTER INCLUDING, BUT NOT LIMITED TO, WARRANTY OF FITNESS FOR PURPOSE
 * OR MERCHANTABILITY, EXCLUSIVITY, OR RESULTS OBTAINED FROM USE OF THE
 * MATERIAL. CARNEGIE MELLON UNIVERSITY DOES NOT MAKE ANY WARRANTY OF ANY KIND
 * WITH RESPECT TO FREEDOM FROM PATENT, TRADEMARK, OR COPYRIGHT INFRINGEMENT.
 * 
 * This material has been approved for public release and unlimited
 * distribution.
 * 
 * DM-0002621
 */

#ifndef __INCLUDE_MFS_CACHE__
#define __INCLUDE_MFS_CACHE__

#include <mutex>
#include <unordered_map>
#include <osv/buf.h>
#include <sys/types.h>
#include <osv/device.h>

#define MFS_BUFFER_SIZE 512

struct mfs_buf {
    void *data;
};

class mfs_node {
public:
    inline mfs_node () {
        bh.data = malloc(MFS_BUFFER_SIZE);
    };

    inline ~mfs_node () {
        free(bh.data);
    }

    inline struct mfs_buf *get() {
        return &bh;
    }

    mfs_node *prev = nullptr;
    mfs_node *next = nullptr;
    uint64_t blkid = 0;

private:
    struct mfs_buf bh;
};

class mfs_queue {
public:
    inline mfs_queue(int capacity): capacity(capacity) {
    }

    inline ~mfs_queue() {
        while (head != nullptr) {
            mfs_node *temp = head;
            head = head->next;
            delete temp;
        }
    }

    void moveToHead(mfs_node *node);

    inline bool isFull() {
        return size == capacity;
    }

    // Returns tail blkid!!!!
    uint64_t newNodeAtHead(uint64_t blkid);

    void destroy(mfs_node *node);

    inline mfs_node *getHead() {
        return head;
    }

private:
    int capacity;
    int size = 0;
    mfs_node *head = nullptr;
    mfs_node *tail = nullptr;
};

class mfs_cache {
public:
    inline mfs_cache(int capacity): queue(capacity) {
    }

    int read(struct device *device, uint64_t blkid, struct mfs_buf **bh);
    void release(struct mfs_buf * bh);

private:
    std::mutex cache_lock;
    std::unordered_map<uint64_t, mfs_node *> lut;
    mfs_queue queue;
};

#endif //__INCLUDE_MFS_CACHE__

