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
#include <boost/asio.hpp>
#include <boost/program_options.hpp>
#include "cpio.hh"

using namespace osv;
using namespace std;
using namespace boost::asio::ip;
namespace po = boost::program_options;

// Want to use boost::filesystem, but too much effort to integrate
extern "C" { int mkdirp(const char *d, mode_t mode); }

class cpio_in_expand : public cpio_in {
public:
    cpio_in_expand(std::string prefix): _prefix(prefix) {};
    virtual void add_file(string name, istream& is) override {
        cout << "Adding " << name << "...\n";
        name = _prefix + name;
        auto pos = name.rfind('/');
        if (pos != name.npos) {
            mkdirp(name.substr(0, pos).c_str(), 0755);
        }
        ofstream os(name);
        os << is.rdbuf();
    }

private:
    std::string _prefix;
};

int main(int ac, char** av)
{
    int port;
    std::string prefix;
    // Declare the supported options.
    po::options_description desc("Allowed options");
    desc.add_options()
        ("help", "produce help message")
        ("port", po::value<int>()->default_value(10000), "set listening port")
        ("prefix", po::value<std::string>()->
                   default_value(std::string("/zfs/zfs/")), "set prefix");

    po::variables_map vm;
    po::store(po::parse_command_line(ac, av, desc), vm);
    po::notify(vm);

    if (vm.count("help")) {
        cout << desc << "\n";
        return 1;
    }

    port = vm["port"].as<int>();
    prefix = vm["prefix"].as<std::string>();

    boost::asio::io_service io_service;
    tcp::acceptor acceptor(io_service, tcp::endpoint(tcp::v4(), port));

    cout << "Waiting for connection from host...\n";
    boost::asio::ip::tcp::iostream socket;
    acceptor.accept(*socket.rdbuf());
    cpio_in_expand expand_files(prefix);
    cpio_in::parse(socket, expand_files);
    sync();
}
