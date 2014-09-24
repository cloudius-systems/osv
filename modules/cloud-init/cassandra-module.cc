/*
 * Copyright (C) 2014 Cloudius Systems, Ltd.
 *
 * This work is open source software, licensed under the terms of the
 * BSD license as described in the LICENSE file in the top-level directory.
 */

#include "cassandra-module.hh"

#include "data-source.hh"
#include "template.hh"

#include <fstream>
#include <map>

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
    dict.insert({"external-ip", ds.external_ip()});
    dict.insert({"internal-ip", ds.internal_ip()});

    cout << "cloud-init: Cassandra configuration:" << endl;
    for (auto&& kv : dict) {
        cout << "  '" << kv.first << "' => '" << kv.second << "'" << endl;
    }

    ifstream input(template_filename);
    if (!input.is_open()) {
        cout << "cloud-init: warning: '" << template_filename << "' template not found." << endl;
        return;
    }
    template_source source(input);
    input.close();

    ofstream output(config_filename, ios::out);
    if (!output.is_open()) {
        cout << "cloud-init: warning: unable to open '" << config_filename << "'. Configuration failed." << endl;
    }
    output << source.expand(dict);
    output.close();
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
