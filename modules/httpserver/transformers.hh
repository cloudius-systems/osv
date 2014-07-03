/*
 * Copyright (C) 2014 Cloudius Systems, Ltd.
 *
 * This work is open source software, licensed under the terms of the
 * BSD license as described in the LICENSE file in the top-level directory.
 */

#ifndef TRANSFORMERS_HH_
#define TRANSFORMERS_HH_

#include "handlers.hh"

namespace httpserver {

/**
 * content_replace replaces variable in a file with a dynamic value.
 * Currently the only parameter we support is {{Host}} -> Host.
 * It would take the host from request and will replace the variable
 * in a file
 *
 * The replacement can be restricted to an extension.
 *
 * We are currently support only one file type for replacement.
 * It could be extend if we will need it
 *
 */
class content_replace : public file_transformer {
public:
    virtual void transform(std::string& content, const http::server::request& req,
                           const std::string& extension) override;
    /**
     * the constructor get the file extension the replace would work on.
     * @param extension file extension, when not set all files extension
     */
    explicit content_replace(const std::string& extension = "")
        : extension(extension)
    {
    }
private:
    std::string extension;
};

}
#endif /* TRANSFORMERS_HH_ */
