/*
 * Copyright (C) 2013 Cloudius Systems, Ltd.
 *
 * This work is open source software, licensed under the terms of the
 * BSD license as described in the LICENSE file in the top-level directory.
 */

#ifndef MATCHER_HH_
#define MATCHER_HH_

#include "common.hh"

#include <string>

namespace httpserver {

/**
 * a base class for the url matching.
 * Each implementation check if the given url matches a criteria
 */
class matcher {
public:

    virtual ~matcher() = default;

    /**
     * check if the given url matches the rule
     * @param url the url to check
     * @param ind the position to start from
     * @param fill the parameters hash
     * @return the end of of the matched part, or std::string::npos if not matched
     */
    virtual size_t match(const std::string& url, size_t ind,
                         parameters& param) = 0;
};

/**
 * Check if the url match a parameter and fill the parameters object
 *
 * Note that a non empty url will always return true with the parameters
 * object filled
 *
 * Assume that the rule is /file/{path}/ and the param_matcher identify
 * the /{path}
 *
 * For all non empty values, match will return true.
 * If the entire url is /file/etc/hosts, and the part that is passed to
 * param_matcher is /etc/hosts, if entire_path is true, the match will be
 * '/etc/hosts' If entire_path is false, the match will be '/etc'
 */
class param_matcher : public matcher {
public:
    /**
     * Constructor
     * @param name the name of the parameter, will be used as the key
     * in the parameters object
     * @param entire_path when set to true, the matched parameters will
     * include all the remaining url until the end of it.
     * when set to false the match will terminate at the next slash
     */
    explicit param_matcher(const std::string& name, bool entire_path = false)
        : name(name)
        , entire_path(entire_path) { }

    virtual size_t match(const std::string& url, size_t ind, parameters& param) override;
private:
    std::string name;
    bool entire_path;
};

/**
 * Check if the url match a predefine string.
 *
 * When parsing a match rule such as '/file/{path}' the str_match would parse
 * the '/file' part
 */
class str_matcher : public matcher {
public:
    /**
     * Constructor
     * @param cmp the string to match
     */
    explicit str_matcher(const std::string& cmp)
        : cmp(cmp)
        , len(cmp.length()) { }

    virtual size_t match(const std::string& url, size_t ind, parameters& param) override;
private:
    std::string cmp;
    unsigned len;
};

}

#endif /* MATCHER_HH_ */
