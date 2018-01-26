/*
 * Copyright (C) 2014 Cloudius Systems, Ltd.
 *
 * This work is open source software, licensed under the terms of the
 * BSD license as described in the LICENSE file in the top-level directory.
 */

#include <sys/types.h>
#include <sys/stat.h>
#include <unistd.h>
#include "cloud-init.hh"
#include "files-module.hh"
#include "server-module.hh"
#include "cassandra-module.hh"
#include "monitor-agent-module.hh"
#include <boost/program_options/variables_map.hpp>
#include <boost/program_options/options_description.hpp>
#include <boost/program_options/parsers.hpp>
#include <osv/debug.hh>
#include <osv/exception_utils.hh>
#include <osv/run.hh>

using namespace std;
using namespace init;
namespace po = boost::program_options;

// config_disk() allows to use NoCloud VM configuration method - see
// http://cloudinit.readthedocs.io/en/0.7.9/topics/datasources/nocloud.html.
// NoCloud method provides two files with cnfiguration data (/user-data and
// /meta-data) on a disk. The disk is required to have label "cidata".
// It can contain ISO9660 or FAT filesystem.
//
// config_disk() checks whether we have a second disk (/dev/vblkX) with
// ISO image, and if there is, it copies the configuration file from
// /user-data to the given file.
// config_disk() returns true if it has successfully read the configuration
// into the requested file. It triest to get configuratioe from first few
// vblk devices, namely vblk1 to vblk10.
//
// OSv implementation limitations:
// The /meta-data file is currently ignored.
// Only ISO9660 filesystem is supported.
// The mandatory "cidata" volume label is not checked.
//
// Example ISO image can be created by running
// cloud-localds cloud-init.img cloud-init.yaml
// The cloud-localds command is provided by cloud-utils package (fedora).
static bool config_disk(const char* outfile) {
    for (int ii=1; ii<=10; ii++) {
        char disk[20];
        char srcfile[] = "/user-data";
        struct stat sb;
        int ret;
        int app_ret = -1;

        snprintf(disk, sizeof(disk), "/dev/vblk%d", ii);
        ret = stat(disk, &sb);
        if (ret != 0) {
            continue;
        }

        std::vector<std::string> cmd = {"/usr/bin/iso-read.so", "-e", srcfile, "-o", outfile, disk};
        osv::run(cmd[0], cmd, &app_ret);
        if (app_ret != 0) {
            debug("cloud-init: warning, %s exited with code %d (%s is not ISO image?)\n", cmd[0], app_ret, disk);
            continue;
        }
        ret = stat(outfile, &sb);
        if (ret != 0) {
            debug("cloud-init: disk %s, stat(%s) failed, errno=%d\n", disk, outfile, errno);
            continue;
        }
        if ((sb.st_mode & S_IFMT) != S_IFREG) {
            debug("cloud-init: disk %s, %s is not a file\n", disk, outfile);
            return false;
        }
        debug("cloud-init: copied file %s -> %s from ISO image %s\n", srcfile, outfile, disk);
        return true;
    }
    return false;
}

int main(int argc, char* argv[])
{
    try {
        po::options_description desc("Allowed options");
        desc.add_options()
            ("help", "produce help message")
            ("skip-error", "do not stop on error")
            ("force-probe", "force data source probing")
            ("file", po::value<std::string>(), "an init file")
            ("server", po::value<std::string>(), "a server to read the file from. must come with a --url")
            ("url", po::value<std::string>(), "a url at the server")
            ("port", po::value<std::string>()->default_value("80"), "a port at the server")
        ;

        po::variables_map config;
        po::store(po::parse_command_line(argc, argv, desc), config);
        po::notify(config);

        if (config.count("help")) {
            std::cerr << desc << "\n";
            return 1;
        }

        osvinit init(config.count("skip-error") > 0, config.count("force-probe") > 0);
        auto scripts = make_shared<script_module>();
        init.add_module(scripts);
        init.add_module(make_shared<mount_module>());
        init.add_module(make_shared<hostname_module>());
        init.add_module(make_shared<files_module>());
        init.add_module(make_shared<server_module>());
        init.add_module(make_shared<include_module>(init));
        init.add_module(make_shared<cassandra_module>());
        init.add_module(make_shared<monitor_agent_module>());

        if (config.count("file")) {
            init.load_file(config["file"].as<std::string>());
        } else if (config.count("server") > 0 && config.count("url") > 0) {
            init.load_url(config["server"].as<std::string>(),
                config["url"].as<std::string>(),
                config["port"].as<std::string>());
        } else if(config_disk("/tmp/config.yaml")) {
            init.load_file("/tmp/config.yaml");
        } else {
            init.load_from_cloud();
        }

        scripts->wait();
    } catch (...) {
        std::cerr << "cloud-init failed: " << what(std::current_exception()) << "\n";
        return 1;
    }

    return 0;
}
