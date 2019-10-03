/*
 * Copyright (C) 2019 Waldemar Kozaczuk
 *
 * This work is open source software, licensed under the terms of the
 * BSD license as described in the LICENSE file in the top-level directory.
 */

#ifndef OSV_OPTIONS_HH
#define OSV_OPTIONS_HH

#include <string>
#include <vector>
#include <map>

//
// The methods in 'options' namespace provide basic functionality intended to help
// parse options by the kernel loader and utility apps like cpiod, httpserver and cloud-init.
namespace options {

using namespace std;

// Iterates over supplied array of arguments and collects them into a map
// where key is an option name and the value is a vector of 0 or more values
// It recognizes only so called 'long' options that start with '--' (double dash) prefix
map<string,vector<string>> parse_options_values(int argc, char** argv,
        function<void (const string&)> error_handler, bool allow_separate_values = true);

// Checks if options_values map contains a 'flag' option of '--<flag>' format
// and returns true if the option found and removes that option from the map
bool extract_option_flag(map<string,vector<string>> &options_values, const string &name,
        function<void (const string&)> error_handler);

// Returns true if options_values contains single-value option (--<key>=<value>) or
// multi-value one (--<key>=<val1>, --<key>=<val2>)
bool option_value_exists(const map<string,vector<string>> &options_values, const string &name);

// Returns the value of a single-value option (--<name>=<key>) and removes it from the options_values
string extract_option_value(map<string,vector<string>> &options_values, const string &name);

// Returns the value of a single-value option (--<name>=<key>), tries to convert to an integer
// and removes it from the options_values
int extract_option_int_value(map<string,vector<string>> &options_values, const string &name,
        function<void (const string&)> error_handler);

// Returns the value of a single-value option (--<name>=<key>), tries to convert to a float
// and removes it from the options_values
float extract_option_float_value(map<string,vector<string>> &options_values, const string &name,
        function<void (const string&)> error_handler);

// Returns the values of a multi-value option (--<key>=<val1>, --<key>=<val2>) and removes
// them from the options_values
vector<string> extract_option_values(map<string,vector<string>> &options_values, const string &name);

};

#endif //OSV_OPTIONS_HH
