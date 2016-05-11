/*
 * Copyright (C) 2014 Cloudius Systems, Ltd.
 *
 * This work is open source software, licensed under the terms of the
 * BSD license as described in the LICENSE file in the top-level directory.
 */

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

using namespace std;
using namespace init;
namespace po = boost::program_options;

// config_disk() checks whether we have a second disk (/dev/vblk1) which
// holds nothing but a configuration file, and if there is, it copies the
// configuration to the given file.
// Currently, the configuration disk must be in a trivial format generated
// by scripts/file2img: A magic header, then the length of the file (in
// decimal), followed by the content.
// config_disk() returns true if it has successfully read the configuration
// into the requested file.
static bool config_disk(const char* outfile) {
    int fd = open("/dev/vblk1", O_RDONLY);
    if (fd < 0) {
        return false;
    }
    char data[512];
    ssize_t r = read(fd, data, sizeof(data));
    static const char* magic = "!file_in_image\n";
    ssize_t magic_len = strlen(magic);
    if (r < magic_len || strncmp(data, magic, magic_len)) {
        close(fd);
        return false;
    }
    debug("cloud-init: found configuration in /dev/vblk1\n");
    int out = open(outfile, O_WRONLY|O_CREAT, 0777);
    if (out < 0) {
        debug("cloud-init: failed to copy configuration to %s\n", outfile);
        close(fd);
        return false;
    }
    unsigned offset = magic_len;
    while (offset < r && data[offset] != '\n')
        offset++;
    offset++; // skip the \n too
    ssize_t filelen = atoi(data + magic_len);
    while (filelen) {
        int len = std::min(filelen, r - offset);
        if (len <= 0) {
            debug("cloud-init: unexpected end of image\n", outfile);
            close(fd);
            close(out);
            return false;
        }
        write(out, data + offset, len);
        filelen -= len;
        offset = 0;
        r = read(fd, data, sizeof(data));
    }
    close(fd);
    close(out);
    return true;
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
