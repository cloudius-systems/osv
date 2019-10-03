/*
 * Copyright (C) 2019 Waldemar Kozaczuk
 *
 * This work is open source software, licensed under the terms of the
 * BSD license as described in the LICENSE file in the top-level directory.
 */

#include <iostream>
#include <functional>
#include <cassert>
#include <osv/options.hh>

namespace options {
using namespace std;
//
// Expects argv to contain individual arguments that are in one of the three forms:
// 1) '--key' or
// 2) '--key=value' or
// 3) 'value'
//
// If 'allow_separate_values' is false, then only first 2 forms are valid and
// the '--key' arguments are identified as 'flag' options (like '--enabled')
// and '--key=value' arguments are treated as key, value pairs
//
// If 'allow_separate_values' is true, then all 3 forms are valid and
// the '--key' arguments NOT followed by 'value' are identified as 'flag' options (like '--enabled')
// and '--key=value' arguments are treated as key, value pairs
// and '--key value' arguments are treated as key, value pairs as well
map<string,vector<string>> parse_options_values(int argc, char** argv,
                                                function<void (const string&)> error_handler,
                                                bool allow_separate_values)
{
    map<string,vector<string>> options_values;

    string key("");
    for (int i = 0; i < argc; i++) {
        string arg(argv[i]);
        //
        // Verify if is a 'long' option (starts with '--')
        if (arg.find("--") != 0) {
            // Not an option
            if (allow_separate_values && !key.empty()) {
                // Treat this arg as a value of the option specified by last key (for example: '--count 5')
                options_values[key].push_back(arg);
                key = "";
                continue;
            }
            else {
                // Separate option values (like '--count 5') are not allowed
                error_handler(string("not an option: ") + arg);
                return options_values;
            }
        }
        //
        // Parse out value if --key=value
        size_t pos = arg.find('=');
        if (string::npos == pos) {
            // Treat as a 'flag' option like for example '--enableXyz'
            key = arg.substr(2);
            // Verify if a non-flag option (non-empty vector) DOES NOT exists already
            auto it = options_values.find(key);
            if (it != options_values.end() && !it->second.empty()) {
                error_handler(string("duplicate option: ") + arg);
                return options_values;
            }
            // Flag: add empty vector to the map
            options_values[key] = vector<string>();
        }
        else {
            // Treat as an option value like for example '--count=5' (single value)
            // or multi-value like '--env=A --env=B=1'
            key = arg.substr(2, pos - 2);
            // Verify if a flag option (empty vector) DOES NOT exists already
            auto it = options_values.find(key);
            if (it != options_values.end() && it->second.empty()) {
                error_handler(string("duplicate option: ") + arg);
                return options_values;
            }

            auto value = arg.substr(pos + 1);
            if (value.empty()) {
                error_handler(string("the required argument for option '--") + key + "' is missing");
                return options_values;
            }
            // (Key,Value) or (Key,[Val1,Val2,..]) - add value to the vector
            options_values[key].push_back(value);
            key = "";
        }
    }

    return options_values;
}

bool extract_option_flag(map<string,vector<string>> &options_values, const string &name, function<void (const string&)> error_handler)
{
    auto it = options_values.find(name);
    if (it != options_values.end()) {
        if (!it->second.empty()) {
            error_handler(string("option '--") + name + "' does not take any arguments");
            return false;
        }

        options_values.erase(it);
        return true;
    } else {
        return false;
    }
}

bool option_value_exists(const map<string,vector<string>> &options_values, const string &name)
{
    auto it = options_values.find(name);
    return it != options_values.end() && !it->second.empty();
}

string extract_option_value(map<string,vector<string>> &options_values, const string &name)
{
    return extract_option_values(options_values, name)[0];
}

static void handle_invalid_argument(const string &name, const string &value, function<void (const string&)> error_handler)
{
    error_handler(string("the argument ('") + value + "') for option '--" + name + "' is invalid");
}

int extract_option_int_value(map<string,vector<string>> &options_values, const string &name, function<void (const string&)> error_handler)
{
    auto value_str = extract_option_values(options_values, name)[0];
    size_t pos;
    int value = 0;

    try {
        value = std::stoi(value_str, &pos);
        if (pos < value_str.length()) {
            handle_invalid_argument(name, value_str, error_handler);
        }
    }
    catch (const invalid_argument& ia) {
        handle_invalid_argument(name, value_str, error_handler);
    }
    return value;
}

float extract_option_float_value(map<string,vector<string>> &options_values, const string &name, function<void (const string&)> error_handler)
{
    auto value_str = extract_option_values(options_values, name)[0];
    size_t pos;
    float value = 0.0f;

    try {
        value =  std::stof(value_str, &pos);
        if (pos < value_str.length()) {
            handle_invalid_argument(name, value_str, error_handler);
        }
    }
    catch (const invalid_argument& ia) {
        handle_invalid_argument(name, value_str, error_handler);
    }
    return value;
}

vector<string> extract_option_values(map<string,vector<string>> &options_values, const string &name)
{
    auto it = options_values.find(name);
    assert(it != options_values.end() && !it->second.empty());
    auto values = it->second;
    options_values.erase(it);
    return values;
}

};
