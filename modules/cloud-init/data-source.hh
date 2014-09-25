/*
 * Copyright (C) 2014 Cloudius Systems, Ltd.
 *
 * This work is open source software, licensed under the terms of the
 * BSD license as described in the LICENSE file in the top-level directory.
 */

#ifndef _OSV_CLOUD_INIT_DATASOURCE_HH
#define _OSV_CLOUD_INIT_DATASOURCE_HH

class data_source {
public:
    virtual ~data_source() {}

    virtual std::string launch_index() = 0;
    virtual std::string reservation_id() = 0;
    virtual std::string external_ip() = 0;
    virtual std::string internal_ip() = 0;
    virtual std::string external_hostname() = 0;
    virtual std::string get_user_data() = 0;
    virtual std::string get_name() = 0;

    /**
     * Returns when this data source is probed successsfuly.
     * Throws exception upon failure.
     */
    virtual void probe() = 0;
};

data_source& get_data_source();

#endif
