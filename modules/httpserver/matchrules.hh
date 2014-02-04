/*
 * Copyright (C) 2013 Cloudius Systems, Ltd.
 *
 * This work is open source software, licensed under the terms of the
 * BSD license as described in the LICENSE file in the top-level directory.
 */

#ifndef MATCH_RULES_HH_
#define MATCH_RULES_HH_

#include "handlers.hh"
#include "matcher.hh"
#include "common.hh"

#include <string>
#include <vector>

namespace httpserver {

/**
 * match_rule check if a url matches criteria, that can contains
 * parameters.
 * the routes object would call the get method with a url and if
 * it matches, the method will return a handler
 * during the matching process, the method fill the parameters object.
 */
class match_rule {
public:
	/**
	 * The destructor deletes matchers.
	 */
	~match_rule() {
		for (auto m : match_list) {
			delete m;
		}
		delete handler;
	}

    /**
     * Constructor with a handler
     * @param handler the handler to return when this match rule is met
     */
    explicit match_rule(handler_base* handler) : handler(handler) { }

    /**
     * Check if url match the rule and return a handler if it does
     * @param url a url to compare against the rule
     * @param params the parameters object, matches parameters will fill
     * the object during the matching process
     * @return a handler if there is a full match or nullptr if not
     */
    handler_base* get(const std::string& url, parameters& params)
    {
        size_t ind = 0;
        for (unsigned int i = 0; i < match_list.size(); i++) {
            if (ind + 1 >= url.length()) {
                return nullptr;
            }
            ind = match_list.at(i)->match(url, ind, params);
            if (ind == std::string::npos) {
                return nullptr;
            }
        }
        return (ind + 1 >= url.length()) ? handler : nullptr;
    }

    /**
     * Add a matcher to the rule
     * @param match the matcher to add
     * @return this
     */
    match_rule& add_matcher(matcher* match)
    {
        match_list.push_back(match);
        return *this;
    }

    /**
     * Add a static url matcher
     * @param str the string to search for
     * @return this
     */
    match_rule& add_str(const std::string& str)
    {
        add_matcher(new str_matcher(str));
        return *this;
    }

    /**
     * add a parameter matcher to the rule
     * @param str the parameter name
     * @param fullpath when set to true, parameter will included all the
     * remaining url until its end
     * @return this
     */
    match_rule& add_param(const std::string& str, bool fullpath = false)
    {
        add_matcher(new param_matcher(str, fullpath));
        return *this;
    }

private:
    std::vector<matcher*> match_list;
    handler_base* handler;
};

}

#endif /* MATCH_RULES_HH_ */
