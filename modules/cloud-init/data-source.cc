/*
 * Copyright (C) 2014 Cloudius Systems, Ltd.
 *
 * This work is open source software, licensed under the terms of the
 * BSD license as described in the LICENSE file in the top-level directory.
 */

#include <thread>

#include "client.hh"
#include "data-source.hh"

#include <osv/debug.hh>
#include <osv/exception_utils.hh>

using namespace init;

const std::string metadata_service = "169.254.169.254";

class gce : public data_source {
public:
    virtual std::string launch_index()
    {
        throw std::runtime_error("Launch index not supported on GCE.");
    }

    virtual std::string reservation_id()
    {
        throw std::runtime_error("Reservation ID not supported on GCE.");
    }

    virtual std::string external_ip()
    {
        return get("/computeMetadata/v1/instance/network-interfaces/0/access-configs/0/external-ip");
    }

    virtual std::string internal_ip()
    {
        return get("instance/network-interfaces/0/ip");
    }

    virtual std::string external_hostname()
    {
        return get("/computeMetadata/v1/instance/hostname");
    }

    virtual std::string get_user_data()
    {
        return get("/computeMetadata/v1/instance/attributes/user-data");
    }

    virtual std::string get_name()
    {
        return "GCE";
    }

    virtual void probe()
    {
        client c;
        c.set_header("Metadata-Flavor", "Google");
        c.get(metadata_service, "/computeMetadata/v1/instance/id");
        if (!c.is_ok()) {
            throw std::runtime_error("Request failed: " + c.get_status());
        }
    }

private:
    virtual std::string get(std::string url)
    {
        client c;
        c.set_header("Metadata-Flavor", "Google");
        c.get(metadata_service, url);

        if (c.get_status() == http_status::NOT_FOUND) {
            return "";
        }

        if (!c.is_ok()) {
            throw std::runtime_error("Request failed: " + c.get_status());
        }

        return c.text();
    }

};

class ec2 : public data_source {
public:
    virtual std::string launch_index()
    {
        client c;
        c.get(metadata_service, "/latest/meta-data/ami-launch-index");
        return c.text();
    }

    virtual std::string reservation_id()
    {
        client c;
        c.get(metadata_service, "/latest/meta-data/reservation-id");
        return c.text();
    }

    virtual std::string external_ip()
    {
        client c;
        c.get(metadata_service, "/latest/meta-data/public-ipv4");
        return c.text();
    }

    virtual std::string internal_ip()
    {
        client c;
        c.get(metadata_service, "/latest/meta-data/local-ipv4");
        return c.text();
    }

    virtual std::string external_hostname()
    {
        client c;
        c.get(metadata_service, "/latest/meta-data/public-hostname");
        return c.text();
    }

    virtual std::string get_user_data()
    {
        client c;
        c.get(metadata_service, "/latest/user-data");
        return c.text();
    }

    virtual std::string get_name()
    {
        return "EC2";
    }

    virtual void probe()
    {
        client c;
        c.get(metadata_service, "/latest/meta-data/ami-id");
        if (!c.is_ok()) {
            throw std::runtime_error("Request failed: " + c.get_status());
        }
    }
};

static std::vector<data_source*> data_sources = { new ec2(), new gce() };

static data_source& probe(data_source& ds) {
    debug("Probing " + ds.get_name() + " ...\n");
    try {
        ds.probe();
        debug("Looks like we're on " + ds.get_name() + "\n");
        return ds;
    } catch (...) {
        throw std::runtime_error(ds.get_name() + ": " + current_what());
    }
}

static data_source& probe_data_source()
{
    for (auto ds : data_sources) {
        try {
            return probe(*ds);
        } catch (...) {
            debug(current_what() + "\n");
        }
    }

    throw std::runtime_error("No data source matched");
}

static data_source* _ds;

data_source& get_data_source()
{
    static std::once_flag flag {};
    std::call_once(flag, [] {
        _ds = &probe_data_source();
    });
    return *_ds;
}
