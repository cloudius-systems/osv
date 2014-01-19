/*
 * Copyright (C) 2014 Cloudius Systems, Ltd.
 *
 * This work is open source software, licensed under the terms of the
 * BSD license as described in the LICENSE file in the top-level directory.
 */

#ifndef JSON_ELEMENTS_HH_
#define JSON_ELEMENTS_HH_

#include <string>
#include <vector>
#include <time.h>
#include <sstream>
#include "formatter.hh"

namespace httpserver {

namespace json {

/**
 * The base class for all json element.
 * Every json element has a name
 * An indication if it was set or not
 * And is this element is mandatory.
 * When a mandatory element is not set
 * this is not a valid object
 */
class json_base_element {
public:
    /**
     * The constructors
     */
    json_base_element()
        : mandatory(false), set(false)
    {
    }

    virtual ~json_base_element() = default;

    /**
     * Check if it's a mandatory parameter
     * and if it's set.
     * @return true if this is not a mandatory parameter
     * or if it is and it's value is set
     */
    virtual bool is_verify()
    {
        return !(mandatory && !set);
    }

    /**
     * returns the internal value in a json format
     * Each inherit class must implement this method
     * @return formated internal value
     */
    virtual std::string to_string() = 0;

    std::string name;
    bool mandatory;
    bool set;
};

/**
 * Basic json element instantiate
 * the json_element template.
 * it adds a value to the base definition
 * and the to_string implementation using the formatter
 */
template<class T>
class json_element : public json_base_element {
public:

    /**
     * the assignment operator also set
     * the set value to true.
     * @param new_value the new value
     * @return the value itself
     */
    json_element &operator=(const T& new_value)
    {
        value = new_value;
        set = true;
        return *this;
    }

    /**
     * The brackets operator
     * @return the value
     */
    const T& operator()() const
    {
        return value;
    }

    /**
     * The to_string return the value
     * formated as a json value
     * @return the value foramted for json
     */
    virtual std::string to_string() override
    {
        return formatter::to_json(value);
    }

private:
    T value;
};

/**
 * json_list is based on std vector implementation.
 *
 * When values are added with push it is set the "set" flag to true
 * hence will be included in the parsed object
 */
template<class T>
class json_list : public json_base_element {
public:

    /**
     * Add an element to the list.
     * @param element a new element that will be added to the list
     */
    void push(const T& element)
    {
        set = true;
        elements.push_back(element);
    }

    virtual std::string to_string() override
    {
        return formatter::to_json(elements);
    }

    std::vector<T> elements;
};

/**
 * The base class for all json objects
 * It holds a list of all the element in it,
 * allowing it implement the to_json method.
 *
 * It also allows iterating over the element
 * in the object, even if not all the member
 * are known in advance and in practice mimic
 * reflection
 */
struct json_base {

    virtual ~json_base() = default;

    /**
     * create a foramted string of the object.
     * @return the object formated.
     */
    virtual std::string to_json() const;

    /**
     * Check that all mandatory elements are set
     * @return true if all mandatory parameters are set
     */
    virtual bool is_verify() const;

    /**
     * Register an element in an object
     * @param element the element to be added
     * @param name the element name
     * @param mandatory is this element mandatory.
     */
    virtual void add(json_base_element* element, std::string name,
                     bool mandatory = false);

    std::vector<json_base_element*> elements;
};

}

}
#endif /* JSON_ELEMENTS_HH_ */
