/*
 * Copyright (C) 2013 Cloudius Systems, Ltd.
 *
 * This work is open source software, licensed under the terms of the
 * BSD license as described in the LICENSE file in the top-level directory.
 */

#ifndef SCSI_COMMON_H
#define SCSI_COMMON_H

#include <osv/types.h>
#include <osv/device.h>
#include <osv/bio.h>
#include <vector>

enum {
    SCSI_OK = 0,
    SCSI_SECTOR_SIZE = 512,
};

// SCSI OPERATION CODE
enum {
    CDB_CMD_TEST_UNIT_READY  = 0x00,
    CDB_CMD_REQUEST_SENSE = 0x03,
    CDB_CMD_INQUIRY = 0x12,
    CDB_CMD_READ_16 = 0x88,
    CDB_CMD_WRITE_16 = 0x8A,
    CDB_CMD_READ_CAPACITY = 0x9E,
    CDB_CMD_SYNCHRONIZE_CACHE_10 = 0x35,
    CDB_CMD_SYNCHRONIZE_CACHE_16 = 0x91,
    CDB_CMD_REPORT_LUNS = 0xA0,
};

// SCSI CDB
struct cdb_test_unit_ready {
    u8 command;
    u8 reserved_01[4];
    u8 control;
} __attribute__((packed));

struct cdb_inquery {
    u8 command;
    u8 reserved_01;
    u8 page_code;
    u16 alloc_len;
    u8 control;
} __attribute__((packed));

struct cdb_read_capacity {
    u8 command;
    u8 service_action;
    u64 reserved_02;
    u32 alloc_len;
    u8 reserved_0e;
    u8 control;
} __attribute__((packed));

struct cdb_request_sense {
    u8 command;
    u8 flags;
    u16 reserved_02;
    u8 alloc_len;
    u8 control;
} __attribute__((packed));

struct cdb_readwrite_16 {
    u8 command;
    u8 flags;
    u64 lba;
    u32 count;
    u8 group_number;
    u8 control;
} __attribute__((packed));

struct cdb_readwrite_10 {
    u8 command;
    u8 flags;
    u32 lba;
    u8 group_number;
    u16 count;
    u8 control;
} __attribute__((packed));

struct cdb_report_luns {
    u8 command;
    u8 reserved_01;
    u8 select_report;
    u8 reserved_03;
    u8 reserved_04;
    u8 reserved_05;
    u32 alloc_len;
    u8 reserved_0a;
    u8 control;
} __attribute__((packed));

// SCSI CDB RESULT
struct cdbres_request_sense {
    u8 resp_code;
    u8 seg_nr;
    u8 flags;
    u32 info;
    u8 add_sense_len;
    u32 cmd_specific_info;
    u8 asc;
    u8 ascq;
    u8 field;
    u8 sense_key[3];
    u8 add_sense_bytes;
} __attribute__((packed));

struct cdbres_inquiry {
    u8 pdt;
    u8 type_modifier;
    u8 flags[2];
    u8 add_len;
    u8 reserved_05[3];
    char vendor[8];
    char product[16];
    char rev[4];
    char vendor_specific[20];
    char reserved_38[40];
    u8 vendor_specific_len;
} __attribute__((packed));

struct cdbres_read_capacity {
    u64 sectors;
    u32 blksize;
    u8 flags1;
    u8 flags2;
    u16 aligned;
    u8 reserved_10[16];
} __attribute__((packed));

struct cdbres_report_luns {
    u32 list_len;
    u32 reserved_04;
    u64 list[256];
} __attribute__((packed));

struct scsi_common_config {
    u16 max_lun = 256;
    u32 max_sectors = 8192; // Maxium 4MB
    u32 cdb_size = 16;
};

class scsi_common_req {
public:
    scsi_common_req(struct bio* bio, u16 _target, u16 _lun, u8 cmd);
    ~scsi_common_req() { };

    struct bio* bio;
    u16 target;
    u16 lun;
    u8 cdb[16];
    u8 response = 0;
    u8 status = 0;
};

class scsi_common {
public:

    virtual int make_request(struct bio* bio) = 0;
    virtual void add_lun(u16 target_id, u16 lun_id) = 0;
    virtual int exec_cmd(struct bio *bio) = 0;
    virtual scsi_common_req *alloc_scsi_req(struct bio *bio, u16 target, u16 lun, u8 cmd) = 0;

    void scan();
    bool cdb_data_rw(const u8 *cdb);
    bool cdb_data_in(const u8 *cdb);
    void exec_inquery(u16 target, u16 lun);
    void exec_test_unit_ready(u16 taget, u16 lun);
    void exec_request_sense(u16 taget, u16 lun);
    std::vector<u16> exec_report_luns(u16 target);
    bool test_lun(u16 target_id, u16 lun_id);
    void exec_read_capacity(u16 target, u16 lun, size_t &devsize);
    int exec_readwrite(u16 target, u16 lun, struct bio *bio, u8 cmd);
    int exec_synccache(u16 target, u16 lun, struct bio *bio, u8 cmd);

    scsi_common_config config;
};
#endif
