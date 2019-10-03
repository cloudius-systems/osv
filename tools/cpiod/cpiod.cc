/*
 * Copyright (C) 2013 Cloudius Systems, Ltd.
 *
 * This work is open source software, licensed under the terms of the
 * BSD license as described in the LICENSE file in the top-level directory.
 */

#include <assert.h>
#include <iostream>
#include <fstream>
#include <string>
#include <system_error>
#include <boost/asio.hpp>
#include "cpio.hh"
#include <sys/mount.h>
#include <errno.h>
#include <stdio.h>
#include <sys/stat.h>
#include <osv/options.hh>

using namespace osv;
using namespace std;
using namespace boost::asio::ip;

// Want to use boost::filesystem, but too much effort to integrate
extern "C" { int mkdirp(const char *d, mode_t mode); }


static void make_directories(std::string path) {
    if (mkdirp(path.c_str(), 0755) < 0 && errno != EEXIST) {
        throw std::system_error(errno, std::system_category(), "mkdirp " + path);
    }
}

// Truncates /a/b/c to /a/b/, and also /a/b/c/// to /a/b/.
std::string parent_path(std::string path)
{
    auto i = path.rbegin();
    while (*i == '/') {
        ++i;
    }
    path.erase(std::find(i, path.rend(), '/').base(), path.end());
    return path;
}

static void change_mode(std::string path, mode_t mode) {
    if (chmod(path.c_str(), mode) < 0) {
        throw std::system_error(errno, std::system_category(), "chmod");
    }
}

class cpio_in_expand : public cpio_in {
public:
    class logger {
    public:
        logger(bool verbose): _verbose(verbose) {};
        template<typename T>
        logger& operator << (const T& x) {
            if (_verbose) {
                std::cout << x;
            }
            return *this;
        }
    public:
        bool _verbose;
    };
public:
    cpio_in_expand(std::string prefix, bool verbose): _prefix(prefix), _log(verbose) {};
    virtual void add_file(string name, istream& is, mode_t mode) override {
        _log << "Adding " << name << "...\n";
        name = add_prefix(name);
        make_directories(parent_path(name));
        ofstream os(name);
        os << is.rdbuf();
        change_mode(name, mode);
    }
    virtual void add_dir(string name, mode_t mode) override {
        _log << "Adding " << name << "...\n";
        name = add_prefix(name);
        make_directories(name);
        change_mode(name, mode);
    }
    virtual void add_symlink(string oldpath, string newpath, mode_t mode) override {
        _log << "Link " << newpath << " to " << oldpath << " ...\n";
        newpath = add_prefix(newpath);
        auto pos = newpath.rfind('/');
        if (pos != newpath.npos) {
            make_directories(newpath.substr(0, pos));
        }
        auto ret = symlink(oldpath.c_str(), newpath.c_str());
        if (ret == -1) {
            throw std::system_error(errno, std::system_category(), "symlink");
        }
    }

private:
    std::string _prefix;
    logger _log;
    std::string add_prefix(std::string path) {
        if (_prefix.empty()) {
            return path;
        }
        path = _prefix + path;
        // If if _prefix + path contains symbolic link to an absolute path,
        // we need to add prefix to that link target as well.
        char buf[1024];
        std::vector<char> s(path.begin(), path.end());
        for (unsigned i = 1; i < path.size(); ++i) {
            if (s[i] != '/') {
                continue;
            }
            s[i] = '\0';
            int n = readlink(s.data(), buf, sizeof(buf) - 1);
            s[i] = '/';
            if (n >= 0 && buf[0] == '/') {
                buf[n] = '\0';
                path = _prefix + buf + "/" +
                        std::string(s.begin() + i + 1, s.end());
                s = std::vector<char>(path.begin(), path.end());
                i = _prefix.size() + n;
            } else if (n < 0 && errno != EINVAL) {
                break;
            }
        }
        return path;
    }
};

static void usage()
{
    std::cout << "Allowed options:\n";
    std::cout << "  --help                produce help message\n";
    std::cout << "  --port arg (=10000)   set listening port\n";
    std::cout << "  --verbose arg (=1)    disable verbose output\n";
    std::cout << "  --prefix arg (=/)     set prefix\n\n";
}

static void handle_parse_error(const std::string &message)
{
    std::cout << message << std::endl;
    usage();
    exit(1);
}

int main(int ac, char** av)
{
    int port = 10000;
    std::string prefix("/");
    bool verbose = true;

    auto options_values = options::parse_options_values(ac - 1, av + 1, handle_parse_error);

    if (options::extract_option_flag(options_values, "help", handle_parse_error)) {
        usage();
	return 1;
    }

    if (options::option_value_exists(options_values, "port")) {
        port = options::extract_option_int_value(options_values, "port", handle_parse_error);
    }

    if (options::option_value_exists(options_values, "prefix")) {
        prefix = options::extract_option_value(options_values, "prefix");
    }

    if (options::option_value_exists(options_values, "verbose")) {
        verbose = options::extract_option_int_value(options_values, "verbose", handle_parse_error) != 0;
    }

    if (!options_values.empty()) {
        for (auto other_option : options_values) {
            std::cout << "Unrecognized option: " << other_option.first << std::endl;
        }

        usage();
        return 1;
    }

    boost::asio::io_service io_service;
    tcp::acceptor acceptor(io_service, tcp::endpoint(tcp::v4(), port));

    cout << "Waiting for connection from host...\n";
    boost::asio::ip::tcp::iostream socket;
    acceptor.accept(*socket.rdbuf());
    cpio_in_expand expand_files(prefix, verbose);
    cpio_in::parse(socket, expand_files);
    sync();

    // File systems mounted while running mkfs.so will be unmounted here.
    if (prefix == "/zfs/zfs") {
        int ret;

        ret = umount("/zfs/zfs");
        if (ret == -1) {
            fprintf(stderr, "umount /zfs/zfs failed, error = %s\n", strerror(errno));
        }
        ret = umount("/zfs");
        if (ret == -1) {
            fprintf(stderr, "umount /zfs failed, error = %s\n", strerror(errno));
        }
    }
    cout << "cpiod finished" << endl;
}
