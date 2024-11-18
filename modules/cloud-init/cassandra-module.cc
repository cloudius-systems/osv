/*
 * Copyright (C) 2014 Cloudius Systems, Ltd.
 *
 * This work is open source software, licensed under the terms of the
 * BSD license as described in the LICENSE file in the top-level directory.
 */

#include "cassandra-module.hh"

#include "data-source.hh"
#include "template.hh"
#include "client.hh"
#include "json.hh"

#include <osv/debug.hh>

#include <fstream>
#include <chrono>
#include <thread>
#include <vector>
#include <map>

using namespace json11;
using namespace init;
using namespace std;

void cassandra_module::handle(const YAML::Node& doc)
{
    constexpr auto config_filename   = "/usr/cassandra/conf/cassandra.yaml";
    constexpr auto template_filename = "/usr/cassandra/conf/cassandra.yaml.template";

    // User config:
    auto dict = to_map(doc);

    // Instance config:
    auto& ds = get_data_source();
    dict.insert({"launch-index"     , ds.launch_index()});
    dict.insert({"reservation-id"   , ds.reservation_id()});
    dict.insert({"external-ip"      , ds.external_ip()});
    dict.insert({"internal-ip"      , ds.internal_ip()});
    dict.insert({"external-hostname", ds.external_hostname()});

    // Runtime config:
    dict.insert({"seeds", reflector_seeds(dict)});

    debugf("cloud-init: cassandra: Configuration:\n");
    for (auto&& kv : dict) {
        debugf("  '%s' => '%s'\n", kv.first.c_str(), kv.second.c_str());
    }

    ifstream input(template_filename);
    if (!input.is_open()) {
        debugf("cloud-init: cassandra: warning: '%s' template not found.\n", template_filename);
        return;
    }
    template_source source(input);
    input.close();

    ofstream output(config_filename, ios::out);
    if (!output.is_open()) {
        debugf("cloud-init: cassandra: warning: unable to open '%s'. Configuration failed.", config_filename);
        return;
    }
    output << source.expand(dict);
    output.close();
}

string cassandra_module::reflector_seeds(map<string, string> dict)
{
    auto response = wait_for_seeds(dict);
    string result;
    auto count = 0;
    for (auto&& seed : response["seeds"].array_items()) {
        if (count++ > 0) {
            result += ", ";
        }
        result += seed.string_value();
    }
    return result;
}

Json cassandra_module::wait_for_seeds(map<string, string> dict)
{
    unsigned nr_nodes = stoi(dict["totalnodes"]);

    constexpr auto reflector_service = "reflector2.datastax.com";

    debugf("cloud-init: cassandra: Using %s as reflector.\n", reflector_service);

    string url = string("/reflector2.php")
               + "?indexid="           + dict["launch-index"]
               + "&reservationid="     + dict["reservation-id"]
               + "&internalip="        + dict["internal-ip"]
               + "&externaldns="       + dict["external-hostname"]
               + "&second_seed_index=" + to_string(nr_nodes)
               + "&third_seed_index="  + to_string(nr_nodes);

    for (;;) {
        client c;
        c.set_header("User-agent", "DataStaxSetup");
        c.get(reflector_service, url);
       
        if (!c.is_ok()) {
            throw std::runtime_error("Request failed: " + c.get_status());
        }
       
        string error;
        auto response = Json::parse(c.text(), error);
        if (!error.empty()) {
            throw std::runtime_error("JSON parsing failed: " + error);
        }
        if (response["seeds"].array_items().size() >= 1) {
            return response;
        }
        constexpr auto seconds = 5;
        debugf("No seeds, retrying in %d seconds ...\n", seconds);
        this_thread::sleep_for(chrono::seconds(seconds));
    }
}

map<string, string> cassandra_module::to_map(const YAML::Node& doc) const
{
    map<string, string> result;
    for (auto&& node : doc) {
        auto key = node.first.as<string>();
        auto value = node.second.as<string>();
        result.insert({key, value});
    }
    return result;
}
