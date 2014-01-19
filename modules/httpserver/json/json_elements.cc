/*
 * Copyright (C) 2014 Cloudius Systems, Ltd.
 *
 * This work is open source software, licensed under the terms of the
 * BSD license as described in the LICENSE file in the top-level directory.
 */

#include "json_elements.hh"
#include <string.h>
#include <string>
#include <vector>
#include <sstream>

using namespace std;

namespace httpserver {

namespace json {

/**
 * The json builder is a helper class
 * To help create a json object
 *
 */
class json_builder {
public:
    json_builder()
        : first(true)
    {
        result.str(OPEN);
    }

    /**
     * add a name value to an object
     * @param name the name of the element
     * @param str the value already formated
     */
    void add(const string& name, const string& str)
    {
        if (!first) {
            result << ",";
            first = false;
        }
        result << '"' << name << "\": " << str;
    }

    /**
     * add a json element to the an object
     * @param element
     */
    void add(json_base_element* element)
    {
        if (element == nullptr || element->set == false) {
            return;
        }
        add(element->name, element->to_string());
    }

    /**
     * Get the string representation of the object
     * @return a string of accumulative object
     */
    string as_json()
    {
        result << CLOSE;
        return result.str();
    }

private:
    static const string OPEN;
    static const string CLOSE;

    stringstream result;
    bool first;

};

const string json_builder::OPEN("{");
const string json_builder::CLOSE("}");

void json_base::add(json_base_element* element, string name, bool mandatory)
{
    element->mandatory = mandatory;
    element->name = name;
    elements.push_back(element);
}

string json_base::to_json() const
{
    json_builder res;
    for (auto i : elements) {
        res.add(i);
    }
    return res.as_json();
}

bool json_base::is_verify() const
{
    for (auto i : elements) {
        if (!i->is_verify()) {
            return false;
        }
    }
    return true;
}

}
}
