/*
 * Copyright (C) 2014 Cloudius Systems, Ltd.
 *
 * This work is open source software, licensed under the terms of the
 * BSD license as described in the LICENSE file in the top-level directory.
 */

#include "file.hh"
#include "routes.hh"
#include "transformers.hh"
#include "autogen/file.json.hh"
#include <iostream>
#include <fstream>
#include <sys/stat.h>
#include <dirent.h>
#include <grp.h>
#include <pwd.h>
#include "json/formatter.hh"
#include <system_error>
#include "connection.hh"
#include <boost/filesystem.hpp>
#include <algorithm>

namespace httpserver {

namespace api {

namespace file {

using namespace json;
using namespace std;
using namespace file_json;

static bool is_true(const http::server::request& req, const string& param)
{
    string val = req.get_query_param(param);
    std::transform(val.begin(), val.end(), val.begin(), ::tolower);
    if (val == "" || val == "false") {
        return false;
    }
    if (val != "true") {
        throw bad_param_exception(string("Invalid value ") + val + " use true/false");
    }
    return true;
}

/**
 * A helper function to set the op and path param
 * It validate that both exists and if not, throw an exception
 * @param params the parameters object
 * @param req the request
 * @param op will hold the op parameter
 * @param path will hold the path parameter
 */
static void set_and_validate_params(parameters* params,
                                    const http::server::request& req, string& op, string& path)
{
    op = req.get_query_param("op");
    path = (*params)["path-par"];
    if (op == "" || path == "") {
        throw bad_param_exception("missing mandatory parameters");
    }
}

/**
 * update stat structure by a file path
 * @param path the full path to a file or directory
 * @param buffer the stat structure to fill
 * throw a not found exception if the file is not found
 * and bad_param_exception on other errors
 */
static void get_stat(const string& path, struct stat& buffer)
{
    if (stat(path.c_str(), &buffer) != 0) {
        if (errno == ENOENT) {
            throw not_found_exception("Not found: '" + path + "'");
        }
        throw bad_param_exception(
            "Error opening: '" + path + "' " + strerror(errno));
    }
}

/**
 * validate that a path exists.
 * if the file or directory not there, throw an not found exception
 * @param path path to the file or directory
 */
static void validate_path(const string& path)
{
    struct stat buffer;
    get_stat(path, buffer);
}

/**
 * a helper function to get a file name from a path
 * @param path the full file name path
 * @return the file name only
 */
static string file_name(const string& path)
{
    unsigned found = path.find_last_of("/");
    if (found == string::npos) {
        return path;
    }
    return path.substr(found + 1);
}

/**
 * Generate a temporary file name in a target directory
 * according to a file name
 *
 * @return a temporary file name
 */
static string tmp_name(const string& file)
{

    unsigned found = file.find_last_of("/");
    string directory = file.substr(0, found);
    char* name_ptr = tempnam(directory.c_str(), nullptr);

    string res = name_ptr;
    free(name_ptr);
    return res;
}

/**
 * A helper function to copy a file, if the destination cannot be open
 * an exception is thrown
 * @param from path to the source file
 * @param to path to the destination file
 */
static void copy(const std::string& from, const std::string& to)
{

    validate_path(from);

    std::ifstream src;
    std::ofstream dst;
    src.exceptions(std::ios::failbit);
    dst.exceptions(std::ios::failbit);
    try {
        src.open(from, std::ios::binary);
    } catch (const std::system_error& e) {
        throw bad_param_exception(
            "Failed opening file '" + from + "' " + e.code().message());
    }
    try {
        dst.open(to, std::ios::binary);
    } catch (const std::system_error& e) {
        throw bad_param_exception(
            "Failed opening file '" + to + "' " + e.code().message());
    }

    try {
        dst << src.rdbuf();
    } catch (const std::system_error& e) {
        throw bad_param_exception(
            "Failed copying '" + from + "' to '" + to + "' "
            + e.code().message());
    }
}

class get_file_handler : public file_interaction_handler {
    virtual void handle(const std::string& path, parameters* params,
                        const http::server::request& req, http::server::reply& rep)
    {
        string opStr, full_path;
        set_and_validate_params(params, req, opStr, full_path);
        ns_getFile::op op = ns_getFile::str2op(opStr);
        switch (op) {
        case ns_getFile::op::GET:
            read(full_path, req, rep);
            break;
        case ns_getFile::op::GETFILESTATUS:
            file_status(full_path, req, rep);
            break;
        case ns_getFile::op::LISTSTATUS:
            list_directory(full_path, req, rep);
            break;
        default:
            throw bad_param_exception("Bad op parameter " + opStr);
        }
    }

    bool file_status(const string& path, const http::server::request& req,
                     http::server::reply& rep)
    {
        struct stat buffer;
        get_stat(path, buffer);
        FileStatusProperties res = get_file_status(path, file_name(path),
                                   buffer);
        rep.content = res.to_json();
        set_headers(rep, "json");
        return true;
    }

    FileStatusProperties get_file_status(const string& path,
                                         const string& name,
                                         const struct stat& buffer)
    {
        FileStatusProperties res;
        res.blockSize = buffer.st_blksize;
        res.modificationTime = buffer.st_mtime;
        res.accessTime = buffer.st_atime;
        struct group gr;
        struct group *result;
        char buf[512];
        if (getgrgid_r(buffer.st_gid, &gr, buf, 512, &result) == 0) {
            res.group = gr.gr_name;
        }
        struct passwd pwd;
        struct passwd *pwdRes;
        if (getpwuid_r(buffer.st_uid, &pwd, buf, 512, &pwdRes) == 0) {
            res.owner = pwd.pw_name;
        }
        sprintf(buf, "%o", buffer.st_mode & 0777);
        res.permission = buf;
        res.replication = buffer.st_nlink;
        res.pathSuffix = name;
        res.length = buffer.st_size;
        unsigned long lng;
        if ((buffer.st_mode & S_IFMT) == S_IFLNK
                && (lng = readlink(path.c_str(), buf, 512)) > 0) {
            buf[lng] = 0;
            res.symlink = buf;
        }

        switch (buffer.st_mode & S_IFMT) {
        case S_IFDIR:
            res.type = "DIRECTORY";
            break;
        case S_IFLNK:
            res.type = "SYMLINK";
            break;
        default:
            res.type = "FILE";
        }

        return res;
    }

    bool list_directory(const string& path, const http::server::request& req,
                        http::server::reply& rep)
    {
        vector<FileStatusProperties> res;
        validate_path(path);
        auto dirp = opendir(path.c_str());
        if (dirp == nullptr) {
            throw bad_param_exception(
                "Failed listing '" + path + "' " + strerror(errno));
        }
        struct dirent entry;
        struct dirent *result;
        while (readdir_r(dirp, &entry, &result) == 0 && result != nullptr) {
            struct stat buffer;
            string name = path + "/" + entry.d_name;
            if (stat(name.c_str(), &buffer) == 0) {
                res.push_back(get_file_status(name, entry.d_name, buffer));
            }
        }

        (void) closedir(dirp);
        rep.content = formatter::to_json(res);
        set_headers(rep, "json");
        return true;
    }
};

class del_file_handler : public handler_base {
    virtual void handle(const std::string& path, parameters* params,
                        const http::server::request& req, http::server::reply& rep)
    {
        string opStr, full_path;
        set_and_validate_params(params, req, opStr, full_path);
        ns_delFile::op op = ns_delFile::str2op(opStr);
        if (op == ns_delFile::op::DELETE) {
            struct stat buffer;
            get_stat(full_path, buffer);
            try {
                if ((buffer.st_mode & S_IFMT) == S_IFDIR
                        && is_true(req, "recursive")) {
                    boost::filesystem::remove_all(full_path);
                } else {
                    boost::filesystem::remove(full_path);
                }
            } catch (const boost::filesystem::filesystem_error& e) {
                throw bad_request_exception(
                    string("Failed deleting ") + e.what());
            }
        } else {
            throw bad_param_exception("Bad op parameter " + opStr);
        }
        set_headers(rep, "json");
    }
};

class post_file_handler : public handler_base {
    virtual void handle(const std::string& path, parameters* params,
                        const http::server::request& req, http::server::reply& rep)
    {
        string full_path = (*params)["path"];

        http::server::connection_function set_name =
            [full_path](http::server::connection& conn)
        {
            conn.get_multipart_parser().set_tmp_file(tmp_name(full_path));
        };
        req.connection_ptr->get_multipart_parser().set_call_back(
            http::server::multipart_parser::WAIT_CONTENT_DISPOSITION,
            set_name);

        http::server::connection_function when_done =
            [full_path](http::server::connection& conn)
        {
            auto target = full_path;
            string from = conn.get_request().get_header("file_name");
            rename(from.c_str(), target.c_str());
        };
        req.connection_ptr->get_multipart_parser().set_call_back(
            http::server::multipart_parser::CLOSED, when_done);

        req.connection_ptr->upload();

        set_headers(rep, "json");
    }
};

class put_file_handler : public handler_base {
    virtual void handle(const std::string& path, parameters* params,
                        const http::server::request& req, http::server::reply& rep)
    {
        string opStr, full_path;
        set_and_validate_params(params, req, opStr, full_path);
        ns_putFile::op op = ns_putFile::str2op(opStr);
        mode_t permission = 0777;
        if ((*params)["permission"] != "") {
            permission = strtol((*params)["permission"].c_str(), nullptr, 8);
        }
        string destination = req.get_query_param("destination");

        switch (op) {
        case ns_putFile::op::MKDIRS:
            try {
                boost::filesystem::create_directories(full_path);
                boost::filesystem::permissions(full_path,
                                               static_cast<boost::filesystem::perms>(permission));
            } catch (const boost::filesystem::filesystem_error& e) {
                throw bad_param_exception(
                    string("Failed creating directory ") + e.what());
            }
            break;
        case ns_putFile::op::RENAME:
            if (destination == "") {
                throw bad_param_exception(
                    "Missing mandatory parameter: destination");
            }
            if (rename(full_path.c_str(), destination.c_str())
                    != 0) {
                throw bad_param_exception(
                    string("Failed renaming ") + strerror(errno));
            }
            break;
        case ns_putFile::op::COPY:
            if (destination == "") {
                throw bad_param_exception(
                    "Missing mandatory parameters: destination");
            }
            copy(full_path, destination);
            break;
        case ns_putFile::op::WRITE:
        {
            auto flags = ios::out;
            flags |= (req.get_query_param("overwrite") == "true")? ios::trunc : ios::app;
            ofstream f;
            f.exceptions ( std::ifstream::failbit | std::ifstream::badbit );
            f.open(full_path,flags);
            f << req.get_query_param("content");
            f.close();
        }
        break;
        default:
            throw bad_param_exception("Bad op parameter " + opStr);
        }
        set_headers(rep, "json");
    }
};

void init(routes& routes)
{
    file_json_init_path("file API");
    getFile.set_handler(new get_file_handler());
    delFile.set_handler(new del_file_handler());
    putFile.set_handler(new put_file_handler());
    upload.set_handler(new post_file_handler());
}

}
}
}
