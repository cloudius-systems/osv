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

#include "mfs_cache.h"

int mfs_cache::read(struct device *device, uint64_t blkid, mfs_buf **bh) {
    cache_lock.lock();
    mfs_node *qnode = lut[blkid];
    if (qnode != nullptr) {
        queue.moveToHead(qnode);
    } else {
        bool full = queue.isFull();
        uint64_t oldblk = queue.newNodeAtHead(blkid);
        if (full) {
            lut.erase(oldblk);
        }
        qnode = queue.getHead();
        lut[blkid] = qnode;
        //read from file system
        struct buf *buf = NULL;
        int error = bread(device, blkid, &buf);
        if (error) {
            cache_lock.unlock();
            queue.destroy(qnode);
            *bh = NULL;
            return error;
        }
        memcpy(qnode->get()->data, buf->b_data, MFS_BUFFER_SIZE);
        brelse(buf);
    }

    *bh = qnode->get();

    return 0;
}

void mfs_cache::release(struct mfs_buf *bh) {
    cache_lock.unlock();
}

void mfs_queue::destroy(mfs_node *node) {
    if (node != NULL) {
        if (head == node)
            head = node->next;
        if (tail == node)
            tail = node->prev;
        if (node->next)
            node->next->prev = node->prev;
        if (node->prev)
            node->prev->next = node->next;
        delete node;
        size--;
    }
}

uint64_t mfs_queue::newNodeAtHead(uint64_t blkid) {
    uint64_t ret = 0;
    if (tail) {
        ret = tail->blkid;
    }

    mfs_node *qnode = NULL;

    // Full
    if (isFull()) {
        qnode = tail;
        tail = qnode->prev;
        tail->next = NULL;
    } else {
        qnode = new mfs_node;
        size++;
        if (!tail) {
            tail = qnode;
        }
    }

    if (head) {
        head->prev = qnode;
    }

    qnode->blkid = blkid;
    qnode->next = head;
    qnode->prev = NULL;
    head = qnode;

    return ret;
}

void mfs_queue::moveToHead(mfs_node *node) {
    if (node == head) {
        return;
    }
    // Check if we are the tail, and if we are not the head
    if (tail == node) {
        tail = node->prev;
    }

    node->prev->next = node->next;

    if (node->next) {
        node->next->prev = node->prev;
    }

    node->next = head;
    if (node->next) {
        node->next->prev = node;
    }

    node->prev = NULL;
    head = node;
}
