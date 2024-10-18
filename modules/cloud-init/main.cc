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
#include <osv/debug.hh>
#include <osv/exception_utils.hh>
#include <osv/run.hh>
#include <osv/options.hh>

using namespace std;
using namespace init;

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
            debugf("cloud-init: warning, %s exited with code %d (%s is not ISO image?)\n", cmd[0].c_str(), app_ret, disk);
            continue;
        }
        ret = stat(outfile, &sb);
        if (ret != 0) {
            debugf("cloud-init: disk %s, stat(%s) failed, errno=%d\n", disk, outfile, errno);
            continue;
        }
        if ((sb.st_mode & S_IFMT) != S_IFREG) {
            debugf("cloud-init: disk %s, %s is not a file\n", disk, outfile);
            return false;
        }
        debugf("cloud-init: copied file %s -> %s from ISO image %s\n", srcfile, outfile, disk);
        return true;
    }
    return false;
}

static void usage()
{
    std::cout << "Allowed options:\n";
    std::cout << "  --help                produce help message\n";
    std::cout << "  --skip-error          do not stop on error\n";
    std::cout << "  --force-probe         force data source probing\n";
    std::cout << "  --file args           an init file\n";
    std::cout << "  --server arg          a server to read the file from. must come with a --url\n";
    std::cout << "  --url arg             a url at the server\n";
    std::cout << "  --port arg (=80)      a port at the server\n\n";
}

static void handle_parse_error(const std::string &message)
{
    std::cout << message << std::endl;
    usage();
    exit(1);
}

int main(int argc, char* argv[])
{
    try {
        auto options_values = options::parse_options_values(argc - 1, argv + 1, handle_parse_error);

        if (options::extract_option_flag(options_values, "help", handle_parse_error)) {
            usage();
            return 1;
        }

        osvinit init(
            options::extract_option_flag(options_values, "skip-error", handle_parse_error),
            options::extract_option_flag(options_values, "force-probe", handle_parse_error)
            );

        auto scripts = make_shared<script_module>();
        init.add_module(scripts);
        init.add_module(make_shared<mount_module>());
        init.add_module(make_shared<hostname_module>());
        init.add_module(make_shared<files_module>());
        init.add_module(make_shared<server_module>());
        init.add_module(make_shared<include_module>(init));
        init.add_module(make_shared<cassandra_module>());
        init.add_module(make_shared<monitor_agent_module>());

        std::string port("80");
        if (options::option_value_exists(options_values, "port")) {
            port = options::extract_option_value(options_values, "port");
        }

        if (options::option_value_exists(options_values, "file")) {
            init.load_file(options::extract_option_value(options_values, "file"));
        } else if (options::option_value_exists(options_values, "server") &&
                   options::option_value_exists(options_values, "url")) {
            init.load_url(options::extract_option_value(options_values, "server"),
                options::extract_option_value(options_values, "url"),
                port);
        } else if(config_disk("/tmp/config.yaml")) {
            init.load_file("/tmp/config.yaml");
        } else {
            init.load_from_cloud();
        }

        if (!options_values.empty()) {
            for (auto other_option : options_values) {
                std::cout << "Unrecognized option: " << other_option.first << std::endl;
            }

            usage();
        }

        scripts->wait();
    } catch (...) {
        std::cerr << "cloud-init failed: " << what(std::current_exception()) << "\n";
        return 1;
    }

    return 0;
}
