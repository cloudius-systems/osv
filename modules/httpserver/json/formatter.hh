/*
 * Copyright (C) 2014 Cloudius Systems, Ltd.
 *
 * This work is open source software, licensed under the terms of the
 * BSD license as described in the LICENSE file in the top-level directory.
 */

#ifndef FORMATTER_HH_
#define FORMATTER_HH_
#include <string>
#include <vector>
#include <time.h>
#include <sstream>

namespace httpserver {

namespace json {

struct json_base;

typedef struct tm date_time;

/**
 * The formatter prints json values in a json format
 * it overload to_json method for each of the supported format
 * all to_json parameters are passed as a pointer
 */
class formatter {
public:

    /**
     * return a json formated string
     * @param str the string to format
     * @return the given string in a json format
     */
    static std::string to_json(const std::string& str);

    /**
     * return a json formated int
     * @param n the int to format
     * @return the given int in a json format
     */
    static std::string to_json(int n);

    /**
     * return a json formated long
     * @param n the long to format
     * @return the given long in a json format
     */
    static std::string to_json(long n);

    /**
     * return a json formated char* (treated as string)
     * @param str the char* to foramt
     * @return the given char* in a json foramt
     */
    static std::string to_json(const char* str);

    /**
     * return a json formated bool
     * @param d the bool to format
     * @return the given bool in a json format
     */
    static std::string to_json(bool d);

    /**
     * return a json formated list of a given vector of params
     * @param vec the vector to format
     * @return the given vector in a json format
     */
    template<typename T>
    static std::string to_json(const std::vector<T>& vec)
    {
        std::stringstream res;
        res.str("[");
        bool first = true;
        for (auto i : vec) {
            if (first) {
                first = false;
            } else {
                res << ",";
            }
            res << to_json(i);
        }
        res << "]";
        return res.str();
    }

    /**
     * return a json formated date_time
     * @param d the date_time to format
     * @return the given date_time in a json format
     */
    static std::string to_json(const date_time& d);

    /**
     * return a json formated json object
     * @param obj the date_time to format
     * @return the given json object in a json format
     */
    static std::string to_json(const json_base& obj);

    /**
     * return a json formated unsigned long
     * @param l unsigned long to format
     * @return the given unsigned long in a json format
     */
    static std::string to_json(unsigned long l);

private:

    constexpr static const char* TIME_FORMAT = "%c";

};

}
}
#endif /* FORMATTER_HH_ */
